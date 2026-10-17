#pragma once

#include <array>
#include <vector>

namespace pf::dsp
{

// Windowed-sinc fractional delay (64 taps, Kaiser window, 256 table phases
// interpolated linearly). Integer delays are bit exact. A delay of D samples
// reads x[n - D + 31] ... x[n - D - 32], so D must be at least kMinDelay.
struct Sinc
{
    static constexpr int kTaps = 64;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kMinDelay = kHalf;
    static constexpr int kPhases = 256;

    // Taps for delay `d`, reversed so they run oldest sample first.
    struct Taps
    {
        double delay = -1.0;
        int whole = 0;                 // floor (delay)
        std::array<float, kTaps> c {};
    };
    static void make (double delay, Taps& out) noexcept;
};

// Mirrored ring buffer: every sample is written twice, so a 64-tap read never
// has to wrap.
class DelayLine
{
public:
    void prepare (int maxDelay);
    void reset() noexcept;

    void push (float x) noexcept
    {
        data[(size_t) pos] = data[(size_t) (pos + size)] = x;
        pos = (pos + 1) & mask;
    }

    // Reads the most recently pushed sample delayed by taps.delay.
    float read (const Sinc::Taps& t) const noexcept
    {
        const float* p = data.data() + ((pos - 1 - t.whole - Sinc::kHalf) & mask);
        float acc = 0.f;
        for (int j = 0; j < Sinc::kTaps; ++j)
            acc += t.c[(size_t) j] * p[j];
        return acc;
    }

private:
    std::vector<float> data;
    int size = 0, mask = 0, pos = 0;
};

} // namespace pf::dsp
