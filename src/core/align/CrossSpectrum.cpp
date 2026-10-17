#include "CrossSpectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pf
{

void CrossSpectrum::reset (int fftSize, double sampleRate)
{
    n = fftSize;
    fs = sampleRate;
    sab.assign ((size_t) numBins(), {});
    saa.assign ((size_t) numBins(), 0.0);
    sbb.assign ((size_t) numBins(), 0.0);
    weightSum = weightSq = 0.0;
}

double CrossSpectrum::omega (int k) const noexcept { return 2.0 * std::numbers::pi * k / n; }

void CrossSpectrum::add (const dsp::cplx* a, const dsp::cplx* b, double w) noexcept
{
    for (int k = 0; k < numBins(); ++k)
    {
        sab[(size_t) k] += w * std::conj (a[k]) * b[k];
        saa[(size_t) k] += w * std::norm (a[k]);
        sbb[(size_t) k] += w * std::norm (b[k]);
    }
    weightSum += w;
    weightSq += w * w;
}

void CrossSpectrum::scale (double f) noexcept
{
    for (auto& v : sab) v *= f;
    for (auto& v : saa) v *= f;
    for (auto& v : sbb) v *= f;
    weightSum *= f;
    weightSq *= f * f;
}

double CrossSpectrum::effectiveFrames() const noexcept
{
    return weightSq > 0.0 ? 0.5 * weightSum * weightSum / weightSq : 0.0;
}

dsp::cplx CrossSpectrum::transfer (int k, const Correction& c, double shiftSamples) const noexcept
{
    // Every frequency above DC turns by the same angle; a shift adds a linear phase.
    const double a = c.totalDeg() * std::numbers::pi / 180.0 - omega (k) * shiftSamples;
    return k == 0 ? dsp::cplx (std::cos (c.totalDeg() * std::numbers::pi / 180.0), 0.0) : std::polar (1.0, a);
}

void CrossSpectrum::bands (const Correction* c, std::vector<Band>& out, int perOctave, double loHz, double hiHz) const
{
    out.clear();
    if (n == 0) return;
    hiHz = std::min (hiHz, 0.5 * fs);
    const int count = (int) std::floor (std::log2 (hiHz / loHz) * perOctave);
    const double step = std::pow (2.0, 1.0 / perOctave);
    const auto turn = c != nullptr ? std::polar (1.0, c->totalDeg() * std::numbers::pi / 180.0) : dsp::cplx (1.0, 0.0);

    float loudest = 0.f;
    for (int i = 0; i < count; ++i)
    {
        const double lo = loHz * std::pow (step, i), hi = lo * step;
        int k0 = (int) std::ceil (lo * n / fs), k1 = (int) std::floor (hi * n / fs);
        k0 = std::max (k0, 1);
        k1 = std::min (k1, numBins() - 1);
        if (k1 < k0)   // narrower than a bin: use the nearest one
            k0 = k1 = std::clamp ((int) std::lround (std::sqrt (lo * hi) * n / fs), 1, numBins() - 1);

        dsp::cplx sum {};
        double mag = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            sum += sab[(size_t) k];
            mag += std::sqrt (saa[(size_t) k] * sbb[(size_t) k]);
        }
        sum *= turn;
        Band b;
        b.hz = (float) std::sqrt (lo * hi);
        b.phaseDeg = (float) (std::arg (sum) * 180.0 / std::numbers::pi);
        b.coherence = mag > 0.0 ? (float) (std::abs (sum) / mag) : 0.f;
        b.weight = (float) mag;   // summed, so wide high bands count like a 1/6-octave meter shows them
        loudest = std::max (loudest, b.weight);
        out.push_back (b);
    }
    for (auto& b : out)
        b.weight = loudest > 0.f && b.weight > 0.f ? std::clamp (1.f + 20.f * std::log10 (b.weight / loudest) / 60.f, 0.f, 1.f) : 0.f;
}

double CrossSpectrum::sumEnergy (const Correction& c, double loHz, double hiHz, double shiftSamples) const noexcept
{
    const int k0 = std::max (1, (int) std::ceil (loHz * n / fs));
    const int k1 = std::min (numBins() - 1, (int) std::floor (hiHz * n / fs));
    double e = 0.0;
    if (std::abs (shiftSamples) < 1e-12)
    {
        dsp::cplx cross {};
        for (int k = k0; k <= k1; ++k)
        {
            e += saa[(size_t) k] + sbb[(size_t) k];
            cross += sab[(size_t) k];
        }
        return e + 2.0 * (cross * std::polar (1.0, c.totalDeg() * std::numbers::pi / 180.0)).real();
    }
    for (int k = k0; k <= k1; ++k)
        e += saa[(size_t) k] + sbb[(size_t) k] + 2.0 * (sab[(size_t) k] * transfer (k, c, shiftSamples)).real();
    return e;
}

double CrossSpectrum::sumGainDb (const Correction& c, double loHz, double hiHz) const noexcept
{
    const double before = sumEnergy ({}, loHz, hiHz), after = sumEnergy (c, loHz, hiHz);
    if (before <= 0.0 || after <= 0.0) return 0.0;
    return 10.0 * std::log10 (after / before);
}

} // namespace pf
