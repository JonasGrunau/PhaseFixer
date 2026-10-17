#pragma once

#include "Correction.h"
#include "dsp/Fft.h"

#include <array>
#include <memory>
#include <vector>

namespace pf
{

// The real-time chain, built like PHA-979: every frequency of the track is
// turned by the same angle,
//     y = polarity * (cos(phi) x - sin(phi) H{x})
// with H a linear-phase FIR Hilbert transformer. The phase response stays
// linear, so the track keeps its timing; the price is a fixed latency of the
// Hilbert half-length plus one convolution block (~48 ms), which the host
// compensates.
//
// The Hilbert FIR runs as a uniformly partitioned FFT convolution. Because the
// filter is real, a stereo pair shares one complex FFT (left in the real part,
// right in the imaginary part). A change of angle or polarity glides along
// the shorter arc over kFadeMs: no click, and no dip in level however far it
// turns (a flip is a turn by 180 degrees).
class Rotator
{
public:
    static constexpr int kBlock = 256;
    static constexpr double kFadeMs = 20.0;

    void prepare (double sampleRate, int numChannels);   // mono or stereo
    void reset() noexcept;

    int latency() const noexcept { return lat; }
    static int latencyFor (double sampleRate) noexcept;

    // Audio thread, before process(). `bypass` keeps the latency but turns
    // the correction off (A/B compare).
    void setTarget (const Correction& c, bool bypass) noexcept;
    void setTarget (double totalDeg, bool bypass) noexcept;   // flip included
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

private:
    void runBlock() noexcept;

    double fs = 48000.0;
    int half = 0, lat = 0, parts = 0, fadeLen = 960, channels = 2;
    std::unique_ptr<dsp::Fft> fft;                 // 2 * kBlock points
    std::vector<std::vector<dsp::cplx>> spectra;   // filter partitions
    std::vector<std::vector<dsp::cplx>> history;   // past input blocks, a ring
    int historyPos = 0;
    std::vector<dsp::cplx> frame, sum, input, output;
    int fill = 0;

    std::array<std::vector<float>, 2> direct;      // dry path, delayed by the latency
    int directMask = 0, directPos = 0;

    double angleNow = 0.0, angleTarget = 0.0, stepAngle = 0.0;
    double cosNow = 1.0, sinNow = 0.0, cosStep = 1.0, sinStep = 0.0;
    int glideLeft = 0;
    bool fresh = true;
};

// The Hilbert component of a whole buffer with the same filter, aligned with
// the buffer (no latency). For the scope: with it, any angle is one multiply-
// add per sample.
std::vector<float> hilbertOffline (const float* x, int n, double sampleRate);
void rotateOffline (const float* x, const float* hilbert, int n, const Correction& c, float* out) noexcept;

// A change of the total turn (degrees, flip included) at a sample.
struct Turn
{
    int sample = 0;
    double deg = 0.0;
};

// The real-time chain run over a whole recording, turns at the same places:
// what the plugin played, without its latency. `turns` is in order. With
// `looped` the recording is one pass of a loop (its end runs into its start),
// otherwise there is silence before and after it.
void renderOffline (const float* const* in, float* const* out, int numCh, int numSamples, double sampleRate,
                    const std::vector<Turn>& turns, bool looped);

} // namespace pf
