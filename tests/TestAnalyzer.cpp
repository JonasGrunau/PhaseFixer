#include "TestUtil.h"
#include "align/Aligner.h"
#include "align/Analyzer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;
using testutil::Signal;

namespace
{
    AlignResult align (const Signal& ref, const Signal& trk, double fs, bool rotation = true, double maxLagMs = 50.0)
    {
        AnalyzerSettings s;
        s.sampleRate = fs;
        s.fitRotation = rotation;
        s.maxLagMs = maxLagMs;
        return analyze (ref.data(), trk.data(), (int) std::min (ref.size(), trk.size()), s);
    }

    std::string describe (const AlignResult& r)
    {
        return "status " + std::to_string ((int) r.status) + " lag " + std::to_string (r.lagSamples)
             + " invert " + std::to_string (r.correction.invert) + " rotate " + std::to_string (r.correction.rotate)
             + " " + std::to_string (r.correction.rotateHz) + " Hz Q " + std::to_string (r.correction.rotateQ)
             + " conf " + std::to_string (r.confidence) + " coh " + std::to_string (r.coherence)
             + " low " + std::to_string (r.lowEndGainDb) + " dB";
    }
} // namespace

TEST_CASE ("Analyzer finds whole-sample delays both ways", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0);
    for (double d : { 0.0, 17.0, -23.0, 480.0, -1200.0, 2300.0 })
    {
        const auto r = align (ref, testutil::delayed (ref, d), fs);
        INFO ("delay " << d << ": " << describe (r));
        CHECK (r.status == AlignStatus::ok);
        CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (d, 0.05));
        CHECK_FALSE (r.correction.invert);
        CHECK_FALSE (r.correction.rotate);
        CHECK (r.confidence > 0.8f);
    }
}

TEST_CASE ("Analyzer finds fractional delays", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 3);
    for (double d : { 0.25, 3.5, 13.37, -7.81 })
    {
        const auto r = align (ref, testutil::delayed (ref, d), fs);
        INFO ("delay " << d << ": " << describe (r));
        CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (d, 0.05));
    }
}

TEST_CASE ("Analyzer flips polarity", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 4);
    const auto r = align (ref, testutil::scaled (testutil::delayed (ref, 5.5), -0.4), fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK (r.correction.invert);
    CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (5.5, 0.05));
    CHECK (r.lowEndGainDb > 3.f);   // was cancelling, now adds up
}

TEST_CASE ("Analyzer sees through a different tone", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 5);
    // Zero-phase colouring, so the true delay stays exactly 40.3 samples.
    auto trk = testutil::Biquad::lowpass (1800.0, 0.7, fs).zeroPhase (testutil::delayed (ref, 40.3));
    trk = testutil::Biquad::peak (120.0, 1.0, 9.0, fs).zeroPhase (trk);
    const auto r = align (ref, trk, fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (40.3, 0.1));
}

TEST_CASE ("Analyzer copes with bleed as loud as the signal", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0, 6);
    double rms = 0.0;
    for (float v : ref) rms += (double) v * v;
    rms = std::sqrt (rms / (double) ref.size());
    const auto trk = testutil::sum (testutil::delayed (ref, 12.6), testutil::noise ((int) ref.size(), rms, 99));
    const auto r = align (ref, trk, fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (12.6, 0.2));
}

TEST_CASE ("Analyzer picks the arrival that sums best, not a reflection", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0, 7);
    {
        const auto trk = testutil::sum (testutil::delayed (ref, 30.0), testutil::scaled (testutil::delayed (ref, 210.0), 0.7));
        const auto r = align (ref, trk, fs, false);
        INFO ("direct louder: " << describe (r));
        CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (30.0, 0.5));
    }
    {
        const auto trk = testutil::sum (testutil::scaled (testutil::delayed (ref, 30.0), 0.5), testutil::delayed (ref, 210.0));
        const auto r = align (ref, trk, fs, false);
        INFO ("reflection louder: " << describe (r));
        CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (210.0, 0.5));
    }
}

TEST_CASE ("Analyzer fits an allpass when the reference was phase-shifted", "[analyzer]")
{
    const double fs = 48000.0;
    const auto src = testutil::drums (fs, 6.0, 8);
    const auto ref = testutil::allpassed (src, 150.0, 0.7, fs);   // e.g. a resonance on one mic only
    const auto trk = testutil::delayed (src, 20.0);

    const auto plain = align (ref, trk, fs, false);
    const auto r = align (ref, trk, fs, true);
    INFO ("plain: " << describe (plain));
    INFO ("rotated: " << describe (r));
    CHECK (r.status == AlignStatus::ok);
    REQUIRE (r.correction.rotate);
    CHECK (r.correction.rotateHz > 150.0 * 0.75);
    CHECK (r.correction.rotateHz < 150.0 * 1.25);
    CHECK (r.lowEndGainDb > plain.lowEndGainDb + 0.5f);
    CHECK_THAT (r.lagSamples, Catch::Matchers::WithinAbs (20.0, 1.0));
}

TEST_CASE ("Analyzer keeps the allpass off when it doesn't help", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 10);
    const auto r = align (ref, testutil::delayed (ref, -88.25), fs);
    INFO (describe (r));
    CHECK_FALSE (r.correction.rotate);
}

TEST_CASE ("Analyzer says so when the signals are unrelated", "[analyzer]")
{
    const double fs = 48000.0;
    const auto r = align (testutil::drums (fs, 6.0, 11), testutil::drums (fs, 6.0, 12), fs);
    INFO (describe (r));
    CHECK ((r.status == AlignStatus::unrelated || r.confidence < 0.4f));
}

TEST_CASE ("Analyzer reports silence and too little signal", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 4.0, 13);
    const Signal silent (ref.size(), 0.f);
    CHECK (align (ref, silent, fs).status == AlignStatus::noTrack);
    CHECK (align (silent, ref, fs).status == AlignStatus::noReference);

    Signal brief (ref.size(), 0.f);
    std::copy (ref.begin(), ref.begin() + (long) (0.3 * fs), brief.begin());
    CHECK (align (brief, brief, fs).status == AlignStatus::notEnoughSignal);
}

TEST_CASE ("Analyzer works at other sample rates", "[analyzer]")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        const auto ref = testutil::drums (fs, 5.0, 14);
        const double d = 3.3e-3 * fs;
        const auto r = align (ref, testutil::delayed (ref, d), fs);
        INFO (fs << " Hz: " << describe (r));
        CHECK (r.status == AlignStatus::ok);
        CHECK_THAT (r.correction.shiftMs, Catch::Matchers::WithinAbs (-3.3, 0.002));
    }
}

TEST_CASE ("Analyzer won't claim a delay outside its range", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0, 15);
    const auto r = align (ref, testutil::delayed (ref, 0.070 * fs), fs);
    INFO (describe (r));
    CHECK (r.status != AlignStatus::ok);
}

TEST_CASE ("Aligning with the result makes the track sum with the reference", "[analyzer][aligner]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 16);
    const auto trk = testutil::scaled (testutil::delayed (ref, 131.7), -0.8);
    const auto r = align (ref, trk, fs);
    REQUIRE (r.status == AlignStatus::ok);

    Aligner a;
    a.prepare (fs, 1);
    a.setTarget (r.correction, 5.0, false);
    Signal y (trk);
    for (size_t s = 0; s < y.size(); s += 512)
    {
        float* ch[] = { y.data() + s };
        a.process (ch, 1, (int) std::min<size_t> (512, y.size() - s));
    }
    // The host delays the reference by the reported latency.
    const auto refLate = testutil::delayed (ref, Aligner::latencyFor (5.0, fs));
    CHECK (testutil::correlation (refLate.data(), y.data(), 4800, (int) y.size() - 4800) > 0.99);
}

TEST_CASE ("Analyzer handles a sustained bass through an amp-like filter", "[analyzer]")
{
    // DI vs miked amp: notes held for a beat, so the correlation repeats every
    // period; the "amp" is a resonant low-pass with real (not zero) phase.
    const double fs = 48000.0;
    const int n = (int) (6.0 * fs);
    Signal di ((size_t) n);
    const double notes[] = { 55.0, 55.0, 65.4, 73.4, 49.0, 55.0, 82.4, 61.7 };
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5);
        const double hz = notes[(int) (t / 0.5) % 8];
        phase += hz / fs;
        const double saw = 2.0 * (phase - std::floor (phase)) - 1.0;
        di[(size_t) i] = (float) (0.4 * saw * std::exp (-beat * 3.0) * std::min (1.0, beat * 400.0));
    }
    const auto amp = testutil::Biquad::lowpass (2500.0, 1.4, fs).run (testutil::delayed (di, 0.0023 * fs));

    const auto r = align (di, amp, fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK (r.lowEndGainDb >= 0.f);
    // The filter adds ~0.1 ms of its own delay on top of 2.3 ms.
    CHECK (r.correction.shiftMs < -2.2);
    CHECK (r.correction.shiftMs > -2.6);
}
