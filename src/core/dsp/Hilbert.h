#pragma once

#include <vector>

namespace pf::dsp
{

// Linear-phase FIR Hilbert transformer: a 90-degree turn of every frequency.
// Odd length 2c + 1, antisymmetric around tap c, Kaiser (beta 6) windowed.
// With c ~ 43 ms it is within 0.1 dB from 20 Hz to Nyquist - 20 Hz and
// falls to zero at DC and Nyquist, as any linear-phase Hilbert must.
struct Hilbert
{
    static int halfLength (double sampleRate);           // c, ~43 ms
    static std::vector<float> design (int half);          // 2c + 1 taps
};

} // namespace pf::dsp
