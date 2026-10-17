#include "FractionalDelay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pf::dsp
{
namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }

    // table[p][j]: reversed taps for the fractional part p / kPhases, each
    // phase normalised to unity gain at DC.
    struct Table
    {
        std::vector<std::array<float, Sinc::kTaps>> rows;

        Table() : rows (Sinc::kPhases + 1)
        {
            constexpr double beta = 7.0;
            const double i0b = besselI0 (beta);
            for (int p = 0; p <= Sinc::kPhases; ++p)
            {
                const double frac = (double) p / Sinc::kPhases;
                double sum = 0.0;
                std::array<double, Sinc::kTaps> c {};
                for (int j = 0; j < Sinc::kTaps; ++j)
                {
                    const double t = Sinc::kHalf - j - frac;
                    const double r = t / Sinc::kHalf;
                    const double w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                    const double s = std::abs (t) < 1e-12 ? 1.0 : std::sin (std::numbers::pi * t) / (std::numbers::pi * t);
                    c[(size_t) j] = s * w;
                    sum += c[(size_t) j];
                }
                for (int j = 0; j < Sinc::kTaps; ++j)
                    rows[(size_t) p][(size_t) j] = (float) (c[(size_t) j] / sum);
            }
        }
    };

    const Table& table()
    {
        static const Table t;
        return t;
    }
} // namespace

void Sinc::make (double delay, Taps& out) noexcept
{
    const auto& rows = table().rows;
    const double whole = std::floor (delay);
    const double pos = (delay - whole) * kPhases;
    const int p = std::min ((int) pos, kPhases - 1);
    const float t = (float) (pos - p);
    const auto& r0 = rows[(size_t) p];
    const auto& r1 = rows[(size_t) p + 1];
    for (int j = 0; j < kTaps; ++j)
        out.c[(size_t) j] = r0[(size_t) j] + t * (r1[(size_t) j] - r0[(size_t) j]);
    out.whole = (int) whole;
    out.delay = delay;
}

void DelayLine::prepare (int maxDelay)
{
    size = 64;
    while (size < maxDelay + Sinc::kTaps + 2)
        size <<= 1;
    mask = size - 1;
    data.assign ((size_t) (2 * size), 0.f);
    pos = 0;
}

void DelayLine::reset() noexcept
{
    std::fill (data.begin(), data.end(), 0.f);
    pos = 0;
}

} // namespace pf::dsp
