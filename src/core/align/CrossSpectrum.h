#pragma once

#include "Correction.h"
#include "dsp/Fft.h"

#include <vector>

namespace pf
{

// Averaged spectra of reference (a) and track (b): S_ab = sum of conj(A) B,
// S_aa, S_bb over frames. Everything the analyzer decides and everything the
// phase display shows is computed from these, for any correction, without
// touching the audio again: correcting the track multiplies S_ab by the
// correction's transfer function (for a rotation, e^{j phi} at every
// frequency above DC).
class CrossSpectrum
{
public:
    void reset (int fftSize, double sampleRate);

    int fftSize() const noexcept { return n; }
    int numBins() const noexcept { return n / 2 + 1; }
    double sampleRate() const noexcept { return fs; }
    double binHz (int k) const noexcept { return k * fs / n; }
    double omega (int k) const noexcept;   // radians per sample

    void add (const dsp::cplx* a, const dsp::cplx* b, double weight) noexcept;
    void scale (double factor) noexcept;    // forgetting, for running estimates
    bool empty() const noexcept { return weightSum <= 0.0; }

    // Number of roughly independent frames behind the estimate (75 % overlap
    // makes neighbours share half their information).
    double effectiveFrames() const noexcept;

    // Transfer function of a correction at bin k, optionally with the track
    // also moved by `shiftSamples` (positive = later), for what-ifs.
    dsp::cplx transfer (int k, const Correction& c, double shiftSamples = 0.0) const noexcept;

    // Display bands (log spaced). Phase of the track relative to the reference
    // in degrees (+ = track ahead), and how well the two add up in that band
    // (|sum| / sum of magnitudes, 1 = perfectly in phase across the band).
    struct Band
    {
        float hz = 0.f;
        float phaseDeg = 0.f;
        float coherence = 0.f;
        float weight = 0.f;       // band level: 1 = loudest band, 0 = 60 dB below it
    };
    void bands (const Correction* c, std::vector<Band>& out, int perOctave = 6,
                double loHz = 20.0, double hiHz = 20000.0) const;

    // Energy of reference + corrected track in [lo, hi] (arbitrary units),
    // optionally with the track also moved by `shiftSamples`.
    double sumEnergy (const Correction& c, double loHz, double hiHz, double shiftSamples = 0.0) const noexcept;
    // How much louder (dB) reference + corrected track sums in [lo, hi] than
    // reference + uncorrected track.
    double sumGainDb (const Correction& c, double loHz, double hiHz) const noexcept;

    std::vector<dsp::cplx> sab;
    std::vector<double> saa, sbb;

private:
    int n = 0;
    double fs = 48000.0;
    double weightSum = 0.0, weightSq = 0.0;
};

} // namespace pf
