#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace pf::dsp
{

// Second-order allpass (RBJ cookbook). Flat magnitude; the phase falls from 0
// at DC through -180 degrees at f0 to -360 degrees at Nyquist, faster for a
// higher Q. The analyzer evaluates the same coefficients with response(), so
// what it fits is exactly what the audio path plays.
struct AllpassCoeffs
{
    double a1 = 0.0, a2 = 0.0;   // H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)

    static AllpassCoeffs make (double f0, double q, double fs) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * std::clamp (f0, 1.0, 0.49 * fs) / fs;
        const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.05));
        const double a0 = 1.0 + alpha;
        return { -2.0 * std::cos (w0) / a0, (1.0 - alpha) / a0 };
    }

    // Frequency response at angular frequency w (radians per sample).
    std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return (a2 + a1 * z1 + z2) / (1.0 + a1 * z1 + a2 * z2);
    }
};

// Transposed direct form II, one channel.
class Allpass
{
public:
    void setCoeffs (const AllpassCoeffs& c) noexcept { k = c; }
    void reset() noexcept { s1 = s2 = 0.0; }

    float process (float x) noexcept
    {
        const double y = k.a2 * x + s1;
        s1 = k.a1 * x - k.a1 * y + s2;
        s2 = x - k.a2 * y;
        return (float) y;
    }

private:
    AllpassCoeffs k;
    double s1 = 0.0, s2 = 0.0;
};

} // namespace pf::dsp
