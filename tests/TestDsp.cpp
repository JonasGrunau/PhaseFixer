#include "TestUtil.h"
#include "dsp/Fft.h"
#include "dsp/Hilbert.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf::dsp;
using Catch::Matchers::WithinAbs;

TEST_CASE ("FFT matches a direct DFT and inverts", "[dsp]")
{
    Fft fft (6);
    const int n = fft.size();
    testutil::Rng rng (3);
    std::vector<cplx> x ((size_t) n), y;
    for (auto& v : x) v = { rng.uniform(), rng.uniform() };
    y = x;
    fft.forward (y.data());
    for (int k = 0; k < n; k += 7)
    {
        cplx d {};
        for (int i = 0; i < n; ++i)
            d += x[(size_t) i] * std::polar (1.0, -2.0 * std::numbers::pi * k * i / n);
        CHECK (std::abs (d - y[(size_t) k]) < 1e-9);
    }
    fft.inverse (y.data());
    for (int i = 0; i < n; ++i)
        CHECK (std::abs (y[(size_t) i] - x[(size_t) i]) < 1e-12);
}

TEST_CASE ("Two real spectra from one complex FFT", "[dsp]")
{
    Fft fft (9);
    const int n = fft.size();
    const auto a = testutil::noise (n, 0.3, 1), b = testutil::noise (n, 0.3, 2);
    std::vector<cplx> fa ((size_t) n / 2 + 1), fb ((size_t) n / 2 + 1), scratch, ra ((size_t) n), rb ((size_t) n);
    fft.forwardRealPair (a.data(), b.data(), n, nullptr, fa.data(), fb.data(), scratch);
    for (int i = 0; i < n; ++i) { ra[(size_t) i] = a[(size_t) i]; rb[(size_t) i] = b[(size_t) i]; }
    fft.forward (ra.data());
    fft.forward (rb.data());
    for (int k = 0; k <= n / 2; ++k)
    {
        CHECK (std::abs (fa[(size_t) k] - ra[(size_t) k]) < 1e-9);
        CHECK (std::abs (fb[(size_t) k] - rb[(size_t) k]) < 1e-9);
    }
}

TEST_CASE ("Hilbert FIR: 90 degrees, flat from 20 Hz to Nyquist - 20 Hz", "[dsp]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int c = Hilbert::halfLength (fs);
        const auto h = Hilbert::design (c);
        REQUIRE ((int) h.size() == 2 * c + 1);
        CHECK (h[(size_t) c] == 0.f);
        for (int k = 1; k <= c; ++k)
            REQUIRE (h[(size_t) (c + k)] == -h[(size_t) (c - k)]);   // antisymmetric: linear phase

        // Response with the centre delay removed: should be -j (a 90 degree lag) at every frequency.
        for (double hz : { 20.0, 30.0, 60.0, 1000.0, 10000.0, fs / 2 - 30.0 })
        {
            const double w = 2.0 * std::numbers::pi * hz / fs;
            cplx r {};
            for (int k = -c; k <= c; ++k)
                r += (double) h[(size_t) (c + k)] * std::polar (1.0, -w * k);
            INFO (fs << " Hz rate, " << hz << " Hz: " << 20.0 * std::log10 (std::abs (r)) << " dB, " << std::arg (r) * 180.0 / std::numbers::pi << " deg");
            CHECK (std::abs (20.0 * std::log10 (std::abs (r))) < (hz < 25.0 ? 0.3 : 0.05));
            CHECK_THAT (std::arg (r), WithinAbs (-std::numbers::pi / 2, 1e-9));
        }
    }
}
