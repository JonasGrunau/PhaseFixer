#include "Hilbert.h"

#include <cmath>
#include <numbers>

namespace pf::dsp
{
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 200; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }
} // namespace

int Hilbert::halfLength (double fs)
{
    return (int) std::lround (0.043 * fs);
}

std::vector<float> Hilbert::design (int c)
{
    constexpr double beta = 6.0;
    const double i0b = besselI0 (beta);
    std::vector<float> h ((size_t) (2 * c + 1), 0.f);
    for (int k = 1; k <= c; k += 2)   // even offsets are zero
    {
        const double r = (double) k / (c + 1);
        const double w = besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
        const double v = 2.0 / (std::numbers::pi * k) * w;
        h[(size_t) (c + k)] = (float) v;
        h[(size_t) (c - k)] = (float) -v;
    }
    return h;
}

} // namespace pf::dsp
