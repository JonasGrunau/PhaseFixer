#include "Aligner.h"

#include <algorithm>
#include <cmath>

namespace pf
{

void Aligner::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    fadeLen = std::max (1, (int) std::lround (kFadeMs * 0.001 * fs));
    maxDelay = latencyFor (limits::kMaxLookaheadMs, fs) + (int) std::ceil (limits::kMaxShiftMs * 0.001 * fs) + 2;

    lines.resize ((size_t) std::max (1, numChannels));
    for (auto& l : lines)
        l.prepare (maxDelay);
    allpasses.assign (lines.size(), {});

    dsp::Sinc::Taps warm;
    dsp::Sinc::make (dsp::Sinc::kMinDelay, warm);   // builds the table off the audio thread
    reset();
}

void Aligner::reset() noexcept
{
    for (auto& l : lines) l.reset();
    for (auto& a : allpasses) a.reset();
    fresh = true;
    fadePos = -1;
    pol = polTarget;
    mix = mixTarget;
    apLogHz = apLogHzTarget;
    apLogQ = apLogQTarget;
    apRunning = false;
}

int Aligner::latencyFor (double lookaheadMs, double sampleRate) noexcept
{
    return (int) std::lround (std::clamp (lookaheadMs, 0.0, limits::kMaxLookaheadMs) * 0.001 * sampleRate) + dsp::Sinc::kHalf;
}

void Aligner::setTarget (const Correction& c, double lookaheadMs, bool bypass) noexcept
{
    const double shift = bypass ? 0.0 : c.shiftSamples (fs);
    const double wanted = latencyFor (lookaheadMs, fs) + shift;
    targetDelay = std::clamp (wanted, (double) dsp::Sinc::kMinDelay, (double) maxDelay);
    clamped = wanted < dsp::Sinc::kMinDelay - 1e-6;

    polTarget = (! bypass && c.invert) ? -1.f : 1.f;
    mixTarget = (! bypass && c.rotate) ? 1.f : 0.f;
    apLogHzTarget = std::log (std::clamp (c.rotateHz, limits::kMinRotateHz, std::min (limits::kMaxRotateHz, 0.45 * fs)));
    apLogQTarget = std::log (std::clamp (c.rotateQ, limits::kMinRotateQ, limits::kMaxRotateQ));

    if (fresh)
    {
        dsp::Sinc::make (targetDelay, heads[(size_t) active]);
        pol = polTarget;
        mix = mixTarget;
        apLogHz = apLogHzTarget;
        apLogQ = apLogQTarget;
        for (auto& a : allpasses)
            a.setCoeffs (dsp::AllpassCoeffs::make (std::exp (apLogHz), std::exp (apLogQ), fs));
        fresh = false;
    }
}

void Aligner::process (float* const* ch, int numCh, int numSamples) noexcept
{
    numCh = std::min (numCh, (int) lines.size());
    const float rampStep = 1.f / (float) fadeLen;
    const double glide = 1.0 - std::exp (-(double) kChunk / (0.03 * fs));

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int n = std::min (kChunk, numSamples - start);

        if (fadePos < 0 && std::abs (targetDelay - heads[(size_t) active].delay) > 1e-9)
        {
            dsp::Sinc::make (targetDelay, heads[(size_t) (1 - active)]);
            fadePos = 0;
        }

        // Per-sample ramps, shared by all channels.
        std::array<float, kChunk> g {}, p {}, m {};
        for (int i = 0; i < n; ++i)
        {
            if (fadePos >= 0)
            {
                const float t = (float) std::min (fadePos + i, fadeLen) / (float) fadeLen;
                g[(size_t) i] = 0.5f - 0.5f * std::cos (3.14159265f * t);
            }
            pol += std::clamp (polTarget - pol, -2.f * rampStep, 2.f * rampStep);
            mix += std::clamp (mixTarget - mix, -rampStep, rampStep);
            p[(size_t) i] = pol;
            m[(size_t) i] = mix;
        }

        const bool wantAllpass = mix > 0.f || mixTarget > 0.f;
        if (wantAllpass)
        {
            const bool moving = std::abs (apLogHzTarget - apLogHz) > 1e-6 || std::abs (apLogQTarget - apLogQ) > 1e-6;
            if (moving || ! apRunning)
            {
                apLogHz += (apLogHzTarget - apLogHz) * glide;
                apLogQ += (apLogQTarget - apLogQ) * glide;
                const auto k = dsp::AllpassCoeffs::make (std::exp (apLogHz), std::exp (apLogQ), fs);
                for (auto& a : allpasses)
                    a.setCoeffs (k);
            }
            apRunning = true;
        }
        else if (apRunning)
        {
            for (auto& a : allpasses) a.reset();
            apRunning = false;
        }

        const auto& h0 = heads[(size_t) active];
        const auto& h1 = heads[(size_t) (1 - active)];
        for (int c = 0; c < numCh; ++c)
        {
            float* x = ch[c] + start;
            auto& line = lines[(size_t) c];
            auto& ap = allpasses[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                line.push (x[i]);
                float y = line.read (h0);
                if (fadePos >= 0)
                    y += g[(size_t) i] * (line.read (h1) - y);
                y *= p[(size_t) i];
                if (apRunning)
                    y += m[(size_t) i] * (ap.process (y) - y);
                x[i] = y;
            }
        }

        if (fadePos >= 0)
        {
            fadePos += n;
            if (fadePos >= fadeLen)
            {
                active = 1 - active;
                fadePos = -1;
            }
        }
    }
}

void applyCorrection (const float* in, int n, const Correction& c, double fs, float* out)
{
    const double shift = c.shiftSamples (fs);
    const int offset = dsp::Sinc::kMinDelay + (int) std::ceil (std::max (0.0, -shift));

    dsp::DelayLine line;
    line.prepare (offset + (int) std::ceil (std::max (0.0, shift)) + 2);
    dsp::Sinc::Taps taps;
    dsp::Sinc::make (offset + shift, taps);

    dsp::Allpass ap;
    ap.setCoeffs (dsp::AllpassCoeffs::make (c.rotateHz, c.rotateQ, fs));
    const float sign = c.invert ? -1.f : 1.f;

    for (int s = 0; s < n + offset; ++s)
    {
        line.push (s < n ? in[s] : 0.f);
        if (s < offset) continue;
        float y = sign * line.read (taps);
        if (c.rotate) y = ap.process (y);
        out[s - offset] = y;
    }
}

} // namespace pf
