#include "Rotator.h"

#include "dsp/Hilbert.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pf
{
namespace
{
    int log2Of (int n)
    {
        int o = 0;
        while ((1 << o) < n) ++o;
        return o;
    }

    constexpr double kPi = std::numbers::pi;

    // Wrapped to (-pi, pi].
    double wrap (double a)
    {
        a = std::remainder (a, 2.0 * kPi);
        return a <= -kPi ? a + 2.0 * kPi : a;
    }

    // cos and sin with the multiples of 90 degrees exact, so a flip is a
    // plain sign change.
    void unit (double a, double& c, double& s)
    {
        c = std::cos (a);
        s = std::sin (a);
        if (std::abs (c) < 1e-12) c = 0.0;
        if (std::abs (s) < 1e-12) s = 0.0;
    }
} // namespace

int Rotator::latencyFor (double sampleRate) noexcept
{
    return dsp::Hilbert::halfLength (sampleRate) + kBlock;
}

void Rotator::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    channels = std::clamp (numChannels, 1, 2);
    half = dsp::Hilbert::halfLength (fs);
    lat = half + kBlock;
    fadeLen = std::max (1, (int) std::lround (kFadeMs * 0.001 * fs));

    const auto h = dsp::Hilbert::design (half);
    const int M = 2 * kBlock;
    fft = std::make_unique<dsp::Fft> (log2Of (M));
    parts = ((int) h.size() + kBlock - 1) / kBlock;
    spectra.assign ((size_t) parts, std::vector<dsp::cplx> ((size_t) M));
    for (int p = 0; p < parts; ++p)
    {
        auto& s = spectra[(size_t) p];
        for (int i = 0; i < kBlock; ++i)
            if (const int t = p * kBlock + i; t < (int) h.size())
                s[(size_t) i] = h[(size_t) t];
        fft->forward (s.data());
    }
    history.assign ((size_t) parts, std::vector<dsp::cplx> ((size_t) M));
    frame.assign ((size_t) M, {});
    sum.assign ((size_t) M, {});
    input.assign ((size_t) kBlock, {});
    output.assign ((size_t) kBlock, {});

    int size = 1;
    while (size < lat + 1) size <<= 1;
    directMask = size - 1;
    for (auto& d : direct) d.assign ((size_t) size, 0.f);
    reset();
}

void Rotator::reset() noexcept
{
    for (auto& h : history) std::fill (h.begin(), h.end(), dsp::cplx {});
    std::fill (frame.begin(), frame.end(), dsp::cplx {});
    std::fill (input.begin(), input.end(), dsp::cplx {});
    std::fill (output.begin(), output.end(), dsp::cplx {});
    for (auto& d : direct) std::fill (d.begin(), d.end(), 0.f);
    historyPos = directPos = fill = 0;
    fresh = true;
}

void Rotator::setTarget (const Correction& c, bool bypass) noexcept
{
    setTarget (c.totalDeg(), bypass);
}

void Rotator::setTarget (double totalDeg, bool bypass) noexcept
{
    const double a = bypass ? 0.0 : wrap (totalDeg * kPi / 180.0);
    if (fresh)
    {
        angleNow = angleTarget = a;
        unit (a, cosNow, sinNow);
        glideLeft = 0;
        fresh = false;
        return;
    }
    if (std::abs (wrap (a - angleTarget)) < 1e-9) return;
    // Along the shorter arc, so the level stays put however far it turns.
    angleTarget = a;
    glideLeft = fadeLen;
    const double step = wrap (a - angleNow) / fadeLen;
    unit (step, cosStep, sinStep);
    stepAngle = step;
}

void Rotator::runBlock() noexcept
{
    const int M = 2 * kBlock;
    // Overlap-save: the frame is the previous block followed by this one.
    std::copy (frame.begin() + kBlock, frame.end(), frame.begin());
    std::copy (input.begin(), input.end(), frame.begin() + kBlock);

    auto& x = history[(size_t) historyPos];
    std::copy (frame.begin(), frame.end(), x.begin());
    fft->forward (x.data());

    std::fill (sum.begin(), sum.end(), dsp::cplx {});
    for (int p = 0; p < parts; ++p)
    {
        const auto& xs = history[(size_t) ((historyPos - p + parts) % parts)];
        const auto& hs = spectra[(size_t) p];
        for (int k = 0; k < M; ++k)
            sum[(size_t) k] += xs[(size_t) k] * hs[(size_t) k];
    }
    historyPos = (historyPos + 1) % parts;

    fft->inverse (sum.data());
    std::copy (sum.begin() + kBlock, sum.end(), output.begin());
}

void Rotator::process (float* const* ch, int numCh, int numSamples) noexcept
{
    numCh = std::min (numCh, channels);
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = ch[0][i], r = numCh > 1 ? ch[1][i] : 0.f;
        input[(size_t) fill] = { l, r };

        direct[0][(size_t) directPos] = l;
        direct[1][(size_t) directPos] = r;
        const auto past = (size_t) ((directPos - lat) & directMask);
        directPos = (directPos + 1) & directMask;

        if (glideLeft > 0)
        {
            if (--glideLeft == 0)
            {
                angleNow = angleTarget;
                unit (angleNow, cosNow, sinNow);
            }
            else
            {
                angleNow += stepAngle;
                const double c = cosNow * cosStep - sinNow * sinStep;
                sinNow = sinNow * cosStep + cosNow * sinStep;
                cosNow = c;
            }
        }

        const auto co = (float) cosNow, si = (float) sinNow;
        const auto& h = output[(size_t) fill];
        ch[0][i] = co * direct[0][past] - si * (float) h.real();
        if (numCh > 1)
            ch[1][i] = co * direct[1][past] - si * (float) h.imag();

        if (++fill == kBlock)
        {
            runBlock();
            fill = 0;
        }
    }
}

std::vector<float> hilbertOffline (const float* x, int n, double fs)
{
    const int c = dsp::Hilbert::halfLength (fs);
    const auto h = dsp::Hilbert::design (c);
    dsp::Fft fft (log2Of (n + (int) h.size()));
    const int N = fft.size();

    // Both FFTs in one: x real, h imaginary; then separate the spectra.
    std::vector<dsp::cplx> z ((size_t) N);
    for (int i = 0; i < n; ++i) z[(size_t) i].real (x[i]);
    for (int i = 0; i < (int) h.size(); ++i) z[(size_t) i].imag (h[(size_t) i]);
    fft.forward (z.data());
    std::vector<dsp::cplx> y ((size_t) N);
    for (int k = 0; k < N; ++k)
    {
        const auto zk = z[(size_t) k], zn = std::conj (z[(size_t) ((N - k) & (N - 1))]);
        const auto xs = 0.5 * (zk + zn), hs = dsp::cplx (0.0, -0.5) * (zk - zn);
        y[(size_t) k] = xs * hs;
    }
    fft.inverse (y.data());

    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i) out[(size_t) i] = (float) y[(size_t) (i + c)].real();
    return out;
}

void rotateOffline (const float* x, const float* h, int n, const Correction& c, float* out) noexcept
{
    double co = 1.0, si = 0.0;
    unit (wrap (c.totalDeg() * kPi / 180.0), co, si);
    for (int i = 0; i < n; ++i) out[i] = (float) co * x[i] - (float) si * h[i];
}

void renderOffline (const float* const* in, float* const* out, int numCh, int n, double fs,
                    const std::vector<Turn>& turns, bool looped)
{
    if (n <= 0) return;
    numCh = std::clamp (numCh, 1, 2);
    Rotator r;
    r.prepare (fs, numCh);
    const int lat = r.latency();

    // Feed: lead-in, the recording, run-out. Output j is input j - lat, so
    // the recording comes out at [lead + lat, lead + lat + n).
    const int lead = lat, total = lead + n + lat;
    std::vector<float> feed[2];
    for (int c = 0; c < numCh; ++c)
    {
        auto& f = feed[c];
        f.assign ((size_t) total, 0.f);
        std::copy (in[c], in[c] + n, f.begin() + lead);
        if (looped)   // the loop's end runs into its start, and round again
            for (int i = 0; i < lead; ++i)
            {
                f[(size_t) i] = in[c][((n - lead + i) % n + n) % n];
                f[(size_t) (lead + n + i)] = in[c][i % n];
            }
    }

    // Same turns at the same places as the real-time run.
    std::vector<Turn> plan;
    plan.push_back ({ 0, turns.empty() ? 0.0 : (looped ? turns.back().deg : turns.front().deg) });
    for (const auto& t : turns)
        plan.push_back ({ lead + std::clamp (t.sample, 0, n), t.deg });
    if (looped && ! turns.empty())
        plan.push_back ({ lead + n, turns.front().deg });

    size_t next = 0;
    for (int pos = 0; pos < total;)
    {
        while (next < plan.size() && plan[next].sample <= pos)
            r.setTarget (plan[next++].deg, false);
        int len = std::min (512, total - pos);
        if (next < plan.size()) len = std::min (len, plan[next].sample - pos);
        float* ch[2] = { feed[0].data() + pos, numCh > 1 ? feed[1].data() + pos : nullptr };
        r.process (ch, numCh, len);
        pos += len;
    }
    for (int c = 0; c < numCh; ++c)
        std::copy (feed[c].begin() + lead + lat, feed[c].begin() + lead + lat + n, out[c]);
}

} // namespace pf
