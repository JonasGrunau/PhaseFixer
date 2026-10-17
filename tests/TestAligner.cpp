#include "TestUtil.h"
#include "align/Aligner.h"
#include "align/Capture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;

namespace
{
    // Runs `x` through an aligner in blocks of 256 with one target throughout.
    testutil::Signal run (Aligner& a, const testutil::Signal& x)
    {
        testutil::Signal y (x);
        for (size_t start = 0; start < y.size(); start += 256)
        {
            float* ch[] = { y.data() + start };
            a.process (ch, 1, (int) std::min<size_t> (256, y.size() - start));
        }
        return y;
    }
} // namespace

TEST_CASE ("Aligner delays by exactly its reported latency at zero shift", "[aligner]")
{
    const double fs = 48000.0;
    const auto x = testutil::noise (20000, 0.3, 7);
    for (double lookahead : { 0.0, 5.0, 20.0 })
    {
        Aligner a;
        a.prepare (fs, 1);
        a.setTarget ({}, lookahead, false);
        const auto y = run (a, x);
        const int lat = Aligner::latencyFor (lookahead, fs);
        for (int i = lat; i < (int) y.size(); ++i)
            REQUIRE (y[(size_t) i] == x[(size_t) (i - lat)]);
    }
}

TEST_CASE ("Aligner shifts earlier and later around its latency", "[aligner]")
{
    const double fs = 48000.0;
    const auto x = testutil::drums (fs, 1.0);
    for (double shiftMs : { -3.217, -0.5, 0.0, 1.111, 12.5 })
    {
        Correction c;
        c.shiftMs = shiftMs;
        Aligner a;
        a.prepare (fs, 1);
        a.setTarget (c, 5.0, false);
        const auto y = run (a, x);
        const double total = Aligner::latencyFor (5.0, fs) + c.shiftSamples (fs);
        // The sinc is only flat to ~20 kHz: compare below that.
        const auto lp = testutil::Biquad::lowpass (16000.0, 0.7, fs);
        const auto want = lp.zeroPhase (testutil::delayed (x, total));
        const auto got = lp.zeroPhase (y);
        INFO ("shift " << shiftMs);
        CHECK (testutil::correlation (got.data(), want.data(), 2000, (int) y.size() - 2000) > 0.99999);
        CHECK_FALSE (a.isShiftClamped());
    }
}

TEST_CASE ("Aligner clamps shifts the lookahead can't cover", "[aligner]")
{
    Aligner a;
    a.prepare (48000.0, 1);
    Correction c;
    c.shiftMs = -6.0;
    a.setTarget (c, 5.0, false);
    CHECK (a.isShiftClamped());
    a.setTarget (c, 10.0, false);
    CHECK_FALSE (a.isShiftClamped());
}

TEST_CASE ("Aligner changes don't click", "[aligner]")
{
    const double fs = 48000.0, hz = 110.0;
    const int n = 48000;
    testutil::Signal x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * std::numbers::pi * hz * i / fs));
    const double maxStep = 0.5 * 2.0 * std::numbers::pi * hz / fs;   // steepest slope of the sine

    Aligner a;
    a.prepare (fs, 1);
    a.setTarget ({}, 20.0, false);
    testutil::Signal y (x);
    double worst = 0.0;
    float last = 0.f;
    for (int start = 0; start < n; start += 128)
    {
        Correction c;
        if (start >= 12000) c.shiftMs = -7.3;      // big jump in time
        if (start >= 24000) c.invert = true;       // polarity flip
        if (start >= 30000) c.rotate = true;       // allpass in
        if (start >= 36000) c.rotateHz = 900.0;    // allpass glides
        a.setTarget (c, 20.0, false);
        float* ch[] = { y.data() + start };
        a.process (ch, 1, 128);
        for (int i = start; i < start + 128; ++i)
        {
            if (i > 2000) worst = std::max (worst, (double) std::abs (y[(size_t) i] - last));
            last = y[(size_t) i];
        }
    }
    CHECK (worst < 1.6 * maxStep);
}

TEST_CASE ("Offline correction matches the real-time chain", "[aligner]")
{
    const double fs = 44100.0;
    const auto x = testutil::drums (fs, 0.8, 9);
    Correction c;
    c.shiftMs = -2.71;
    c.invert = true;
    c.rotate = true;
    c.rotateHz = 240.0;
    c.rotateQ = 1.3;

    Aligner a;
    a.prepare (fs, 1);
    a.setTarget (c, 5.0, false);
    const auto rt = run (a, x);
    testutil::Signal off (x.size());
    applyCorrection (x.data(), (int) x.size(), c, fs, off.data());

    const int lat = Aligner::latencyFor (5.0, fs);
    double err = 0.0;
    for (int i = 4000; i < (int) x.size() - 4000; ++i)
        err = std::max (err, (double) std::abs (rt[(size_t) (i + lat)] - off[(size_t) i]));
    CHECK (err < 1e-4);
}

TEST_CASE ("Capture keeps the last seconds and finds shared hits", "[capture]")
{
    const double fs = 48000.0;
    Capture cap;
    cap.prepare (fs, 4.0);
    const auto ref = testutil::drums (fs, 3.0, 2);
    const auto trk = testutil::delayed (ref, 30.0);
    for (size_t i = 0; i < ref.size(); i += 512)
        cap.push (ref.data() + i, trk.data() + i, (int) std::min<size_t> (512, ref.size() - i));

    CHECK (cap.written() == (int64_t) ref.size());
    const auto act = cap.activity (3.0);
    CHECK (act.shared > 2.0);

    std::vector<float> r (1000), t (1000);
    REQUIRE (cap.copy (100000, 1000, r.data(), t.data()));
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (r[(size_t) i] == ref[(size_t) (100000 + i)]);
        REQUIRE (t[(size_t) i] == trk[(size_t) (100000 + i)]);
    }
    CHECK_FALSE (cap.copy ((int64_t) ref.size() - 10, 100, r.data(), t.data()));   // not written yet

    float score = 0.f;
    const auto hit = cap.strongestHit (2.0, 0, score);
    CHECK (hit >= 0);
    CHECK (score > 3.f);
}
