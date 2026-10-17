#pragma once

namespace pf
{

// What is done to the track so it lines up with the reference: shifted in
// time, maybe flipped, maybe run through one allpass.
struct Correction
{
    double shiftMs = 0.0;     // negative moves the track earlier
    bool invert = false;
    bool rotate = false;      // allpass on
    double rotateHz = 200.0;
    double rotateQ = 0.7;

    double shiftSamples (double fs) const noexcept { return shiftMs * 0.001 * fs; }
};

namespace limits
{
    constexpr double kMaxShiftMs = 50.0;
    constexpr double kMaxLookaheadMs = 50.0;
    constexpr double kMinRotateHz = 20.0, kMaxRotateHz = 20000.0;
    constexpr double kMinRotateQ = 0.3, kMaxRotateQ = 8.0;
}

} // namespace pf
