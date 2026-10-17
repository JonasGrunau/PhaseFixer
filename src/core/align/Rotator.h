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
// right in the imaginary part). Angle and polarity glide over kFadeMs, so
// turning the knob never clicks.
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

    float cosNow = 1.f, sinNow = 0.f, cosTarget = 1.f, sinTarget = 0.f;
    bool fresh = true;
};

// The Hilbert component of a whole buffer with the same filter, aligned with
// the buffer (no latency). For the scope: with it, any angle is one multiply-
// add per sample.
std::vector<float> hilbertOffline (const float* x, int n, double sampleRate);
void rotateOffline (const float* x, const float* hilbert, int n, const Correction& c, float* out) noexcept;

} // namespace pf
