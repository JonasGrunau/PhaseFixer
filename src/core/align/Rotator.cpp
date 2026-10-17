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

    void turn (const Correction& c, bool bypass, float& cosOut, float& sinOut)
    {
        const double a = bypass ? 0.0 : c.phaseDeg * std::numbers::pi / 180.0;
        const double sign = (! bypass && c.invert) ? -1.0 : 1.0;
        cosOut = (float) (sign * std::cos (a));
        sinOut = (float) (sign * std::sin (a));
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
    turn (c, bypass, cosTarget, sinTarget);
    if (fresh)
    {
        cosNow = cosTarget;
        sinNow = sinTarget;
        fresh = false;
    }
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
    const float step = 1.f / (float) fadeLen;
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = ch[0][i], r = numCh > 1 ? ch[1][i] : 0.f;
        input[(size_t) fill] = { l, r };

        direct[0][(size_t) directPos] = l;
        direct[1][(size_t) directPos] = r;
        const auto past = (size_t) ((directPos - lat) & directMask);
        directPos = (directPos + 1) & directMask;

        cosNow += std::clamp (cosTarget - cosNow, -step, step);
        sinNow += std::clamp (sinTarget - sinNow, -step, step);

        const auto& h = output[(size_t) fill];
        ch[0][i] = cosNow * direct[0][past] - sinNow * (float) h.real();
        if (numCh > 1)
            ch[1][i] = cosNow * direct[1][past] - sinNow * (float) h.imag();

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
    float co = 1.f, si = 0.f;
    turn (c, false, co, si);
    for (int i = 0; i < n; ++i) out[i] = co * x[i] - si * h[i];
}

} // namespace pf
