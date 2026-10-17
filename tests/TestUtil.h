#pragma once

#include "dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace testutil
{

using Signal = std::vector<float>;

struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    double uniform()   // [-1, 1)
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (double) (s >> 11) / (double) (1ull << 52) - 1.0;
    }
};

// A drum loop: kick, snare and hats with a little timing and level variation,
// over a faint noise floor. Wideband with a strong low end, like a multi-mic
// drum recording.
inline Signal drums (double fs, double seconds, uint64_t seed = 1)
{
    const int n = (int) (fs * seconds);
    Signal x ((size_t) n, 0.f);
    Rng rng (seed);
    for (auto& v : x) v = (float) (0.0003 * rng.uniform());

    const double eighth = 60.0 / 120.0 / 2.0;
    for (int step = 0; step * eighth < seconds; ++step)
    {
        const int at = (int) ((step * eighth + 0.004 * rng.uniform() + 0.01) * fs);
        const double vel = 0.75 + 0.25 * rng.uniform();
        const int kind = step % 4 == 0 ? 0 : step % 4 == 2 ? 1 : 2;
        const int len = (int) ((kind == 0 ? 0.35 : kind == 1 ? 0.25 : 0.06) * fs);
        double phase = 0.0, lp = 0.0;
        for (int i = 0; i < len && at + i < n; ++i)
        {
            const double t = i / fs;
            double v = 0.0;
            if (kind == 0)   // kick: falling sine plus a click
            {
                phase += 2.0 * std::numbers::pi * (50.0 + 90.0 * std::exp (-t * 30.0)) / fs;
                v = 0.8 * std::sin (phase) * std::exp (-t * 9.0) + 0.3 * rng.uniform() * std::exp (-t * 400.0);
            }
            else if (kind == 1)   // snare: tone plus noise
            {
                phase += 2.0 * std::numbers::pi * 185.0 / fs;
                v = 0.35 * std::sin (phase) * std::exp (-t * 25.0) + 0.45 * rng.uniform() * std::exp (-t * 18.0);
            }
            else   // hat: high-passed noise
            {
                const double w = rng.uniform();
                lp += 0.6 * (w - lp);
                v = 0.25 * (w - lp) * std::exp (-t * 60.0);
            }
            x[(size_t) (at + i)] += (float) (vel * v);
        }
    }
    return x;
}

// One kick (falling sine plus a click) into x at `at`.
inline void addKick (Signal& x, int at, double fs, double vel, Rng& rng)
{
    double phase = 0.0;
    for (int i = 0; i < (int) (0.4 * fs) && at + i < (int) x.size(); ++i)
    {
        const double t = i / fs;
        phase += 2.0 * std::numbers::pi * (50.0 + 90.0 * std::exp (-t * 30.0)) / fs;
        x[(size_t) (at + i)] += (float) (vel * (0.8 * std::sin (phase) * std::exp (-t * 9.0) + 0.2 * rng.uniform() * std::exp (-t * 400.0)));
    }
}

// Kicks on every beat at 120 BPM, a little loose, over a faint noise floor.
inline Signal kicks (double fs, double seconds, uint64_t seed = 1)
{
    Signal x ((size_t) (fs * seconds), 0.f);
    Rng rng (seed);
    for (auto& v : x) v = (float) (0.0003 * rng.uniform());
    for (double t = 0.05; t < seconds; t += 0.5)
        addKick (x, (int) ((t + 0.004 * rng.uniform()) * fs), fs, 0.85 + 0.15 * rng.uniform(), rng);
    return x;
}

inline Signal noise (int n, double rms, uint64_t seed)
{
    Signal x ((size_t) n);
    Rng rng (seed);
    for (auto& v : x) v = (float) (rms * std::sqrt (3.0) * rng.uniform());
    return x;
}

// Exact fractional delay (frequency domain, zero padded). Positive = later.
inline Signal delayed (const Signal& x, double d)
{
    const int n = (int) x.size();
    int order = 0;
    while ((1 << order) < n + 2 * (int) std::ceil (std::abs (d)) + 64) ++order;
    pf::dsp::Fft fft (order);
    const int N = fft.size();
    std::vector<pf::dsp::cplx> z ((size_t) N);
    for (int i = 0; i < n; ++i) z[(size_t) i] = x[(size_t) i];
    fft.forward (z.data());
    for (int k = 0; k < N; ++k)
    {
        const int f = k <= N / 2 ? k : k - N;
        if (k == N / 2) { z[(size_t) k] *= std::cos (std::numbers::pi * d); continue; }
        z[(size_t) k] *= std::polar (1.0, -2.0 * std::numbers::pi * f * d / N);
    }
    fft.inverse (z.data());
    Signal y ((size_t) n);
    for (int i = 0; i < n; ++i) y[(size_t) i] = (float) z[(size_t) ((i) % N)].real();
    return y;
}

inline Signal scaled (Signal x, double g)
{
    for (auto& v : x) v = (float) (v * g);
    return x;
}

inline Signal sum (const Signal& a, const Signal& b)
{
    Signal y (a);
    for (size_t i = 0; i < y.size() && i < b.size(); ++i) y[i] += b[i];
    return y;
}

// RBJ biquad, run forward (or forward and backward for zero phase).
struct Biquad
{
    double b0, b1, b2, a1, a2;

    static Biquad lowpass (double f, double q, double fs)
    {
        const double w = 2.0 * std::numbers::pi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        return { (1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    static Biquad peak (double f, double q, double gainDb, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * std::numbers::pi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        const double a0 = 1.0 + al / A;
        return { (1.0 + al * A) / a0, -2.0 * c / a0, (1.0 - al * A) / a0, -2.0 * c / a0, (1.0 - al / A) / a0 };
    }

    Signal run (const Signal& x) const
    {
        Signal y (x.size());
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double v = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x[i]; y2 = y1; y1 = v;
            y[i] = (float) v;
        }
        return y;
    }
    Signal zeroPhase (const Signal& x) const
    {
        Signal r (run (x));
        std::reverse (r.begin(), r.end());
        r = run (r);
        std::reverse (r.begin(), r.end());
        return r;
    }
};

// Every frequency turned by `deg` (exact, frequency domain): what an ideal
// PHA-979 rotation does. DC and Nyquist only get the cosine part.
inline Signal rotated (const Signal& x, double deg)
{
    const int n = (int) x.size();
    int order = 0;
    while ((1 << order) < n + 8192) ++order;
    pf::dsp::Fft fft (order);
    const int N = fft.size();
    std::vector<pf::dsp::cplx> z ((size_t) N);
    for (int i = 0; i < n; ++i) z[(size_t) i] = x[(size_t) i];
    fft.forward (z.data());
    const double a = deg * std::numbers::pi / 180.0;
    for (int k = 0; k < N; ++k)
    {
        if (k == 0 || k == N / 2) z[(size_t) k] *= std::cos (a);
        else z[(size_t) k] *= std::polar (1.0, k < N / 2 ? a : -a);
    }
    fft.inverse (z.data());
    Signal y ((size_t) n);
    for (int i = 0; i < n; ++i) y[(size_t) i] = (float) z[(size_t) i].real();
    return y;
}

// Normalised correlation of a and b over [from, to).
inline double correlation (const float* a, const float* b, int from, int to)
{
    double ab = 0, aa = 0, bb = 0;
    for (int i = from; i < to; ++i)
    {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    return ab / std::sqrt (aa * bb + 1e-30);
}

} // namespace testutil
