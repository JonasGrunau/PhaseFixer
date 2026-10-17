#pragma once

#include "Correction.h"
#include "dsp/Allpass.h"
#include "dsp/FractionalDelay.h"

#include <array>
#include <vector>

namespace pf
{

// The real-time chain on the track: fractional delay -> polarity -> allpass.
// Every channel gets the same correction.
//
// Moving the track earlier needs lookahead. The chain always delays by
// lookahead + Sinc::kHalf + shift, and reports lookahead + Sinc::kHalf as its
// latency, so after the host compensates the net move is exactly `shift`.
// Shifts earlier than -lookahead are clamped (isShiftClamped()).
//
// Changes never click: a new delay crossfades from the old read head to a new
// one over kFadeMs, polarity ramps through zero, and the allpass fades in and
// out while its frequency and Q glide.
class Aligner
{
public:
    static constexpr double kFadeMs = 20.0;

    void prepare (double sampleRate, int numChannels);
    void reset() noexcept;

    static int latencyFor (double lookaheadMs, double sampleRate) noexcept;

    // Audio thread, before process(). `bypass` keeps the latency but turns
    // the correction off (A/B compare).
    void setTarget (const Correction& c, double lookaheadMs, bool bypass) noexcept;
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    bool isShiftClamped() const noexcept { return clamped; }
    double currentDelay() const noexcept { return heads[(size_t) active].delay; }

private:
    static constexpr int kChunk = 32;

    double fs = 48000.0;
    int maxDelay = 0, fadeLen = 960;
    std::vector<dsp::DelayLine> lines;
    std::vector<dsp::Allpass> allpasses;

    std::array<dsp::Sinc::Taps, 2> heads;
    int active = 0, fadePos = -1;
    double targetDelay = dsp::Sinc::kMinDelay;
    bool clamped = false, fresh = true;

    float pol = 1.f, polTarget = 1.f;
    float mix = 0.f, mixTarget = 0.f;
    double apLogHz = 0.0, apLogQ = 0.0, apLogHzTarget = 0.0, apLogQTarget = 0.0;
    bool apRunning = false;
};

// Applies a correction to a whole buffer offline, in the buffer's own time
// base (no latency): out[t] = polarity * allpass (in[t - shift]). Samples
// outside the input count as silence. For the scope.
void applyCorrection (const float* in, int n, const Correction& c, double sampleRate, float* out);

} // namespace pf
