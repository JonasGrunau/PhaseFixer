#include "TestUtil.h"
#include "dsp/Allpass.h"
#include "dsp/Fft.h"
#include "dsp/FractionalDelay.h"

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

TEST_CASE ("Sinc delay is exact for whole samples", "[dsp]")
{
    const auto x = testutil::noise (2000, 0.5, 4);
    for (int d : { 32, 33, 100, 517 })
    {
        DelayLine line;
        line.prepare (600);
        Sinc::Taps t;
        Sinc::make (d, t);
        for (int i = 0; i < (int) x.size(); ++i)
        {
            line.push (x[(size_t) i]);
            const float y = line.read (t);
            if (i >= d) REQUIRE (y == x[(size_t) (i - d)]);
        }
    }
}

TEST_CASE ("Sinc delay: fractional phase and flat response", "[dsp]")
{
    const double fs = 48000.0;
    for (double frac : { 0.25, 0.5, 0.731 })
        for (double hz : { 100.0, 1000.0, 8000.0, 16000.0 })
        {
            const double d = 40.0 + frac;
            DelayLine line;
            line.prepare (100);
            Sinc::Taps t;
            Sinc::make (d, t);
            double err = 0.0, ein = 0.0, eout = 0.0;
            const int n = 9600;
            for (int i = 0; i < n; ++i)
            {
                line.push ((float) std::sin (2.0 * std::numbers::pi * hz * i / fs));
                const double y = line.read (t);
                if (i > 200)
                {
                    const double want = std::sin (2.0 * std::numbers::pi * hz * (i - d) / fs);
                    err += (y - want) * (y - want);
                    ein += want * want;
                    eout += y * y;
                }
            }
            INFO (frac << " @ " << hz << " Hz");
            const double gainDb = 10.0 * std::log10 (eout / ein);
            CHECK_THAT (gainDb, WithinAbs (0.0, hz <= 8000.0 ? 0.01 : 0.15));
            CHECK (10.0 * std::log10 (err / ein) < (hz <= 8000.0 ? -60.0 : -25.0));
        }
}

TEST_CASE ("Allpass: unity magnitude, -180 degrees at f0, process matches response", "[dsp]")
{
    const double fs = 48000.0, f0 = 300.0;
    const auto k = AllpassCoeffs::make (f0, 0.9, fs);
    for (double hz : { 20.0, 100.0, 1000.0, 10000.0 })
        CHECK_THAT (std::abs (k.response (2.0 * std::numbers::pi * hz / fs)), WithinAbs (1.0, 1e-9));
    CHECK_THAT (std::abs (std::arg (k.response (2.0 * std::numbers::pi * f0 / fs))), WithinAbs (std::numbers::pi, 1e-6));

    // A sine through process() comes out with the phase response() predicts.
    const double hz = 450.0;
    Allpass ap;
    ap.setCoeffs (k);
    const auto h = k.response (2.0 * std::numbers::pi * hz / fs);
    double err = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        const double y = ap.process ((float) std::sin (2.0 * std::numbers::pi * hz * i / fs));
        if (i > 4800)
            err = std::max (err, std::abs (y - std::abs (h) * std::sin (2.0 * std::numbers::pi * hz * i / fs + std::arg (h))));
    }
    CHECK (err < 1e-4);
}
