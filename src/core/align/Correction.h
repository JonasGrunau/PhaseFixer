#pragma once

#include <cmath>

namespace pf
{

// What is done to the track so it sums with the reference, the way Voxengo's
// PHA-979 does it: every frequency is rotated by the same angle (linear phase,
// so nothing moves in time), plus an optional polarity flip. A flip is a
// rotation by 180 degrees; it is kept separate because it is exact and it is
// how engineers think about it.
struct Correction
{
    double phaseDeg = 0.0;   // -180 .. 180
    bool invert = false;

    // The whole turn in degrees, flip included, wrapped to (-180, 180].
    double totalDeg() const noexcept
    {
        double d = phaseDeg + (invert ? 180.0 : 0.0);
        d = std::fmod (d + 180.0, 360.0);
        if (d <= 0.0) d += 360.0;
        return d - 180.0;
    }

    // From a total turn: |rotation| <= 90 degrees, with a flip for the rest.
    static Correction fromTotal (double deg) noexcept
    {
        Correction c;
        c.phaseDeg = Correction { deg, false }.totalDeg();
        if (std::abs (c.phaseDeg) > 90.0)
        {
            c.invert = true;
            c.phaseDeg = Correction { c.phaseDeg - 180.0, false }.totalDeg();
        }
        return c;
    }
};

namespace limits
{
    constexpr double kMaxPhaseDeg = 180.0;
    constexpr double kMaxLagMs = 50.0;   // how far apart the analyzer looks for the time offset
}

} // namespace pf
