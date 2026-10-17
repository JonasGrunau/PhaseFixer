#include "TestUtil.h"
#include "align/Analyzer.h"
#include "align/Rotator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;
using testutil::Signal;
using Catch::Matchers::WithinAbs;

namespace
{
    AlignResult align (const Signal& ref, const Signal& trk, double fs)
    {
        AnalyzerSettings s;
        s.sampleRate = fs;
        return analyze (ref.data(), trk.data(), (int) std::min (ref.size(), trk.size()), s);
    }

    std::string describe (const AlignResult& r)
    {
        return "status " + std::to_string ((int) r.status) + " phase " + std::to_string (r.correction.phaseDeg)
             + " invert " + std::to_string (r.correction.invert) + " total " + std::to_string (r.correction.totalDeg())
             + " conf " + std::to_string (r.confidence) + " coh " + std::to_string (r.coherence)
             + " low " + std::to_string (r.lowEndGainDb) + " dB full " + std::to_string (r.sumGainDb)
             + " dB offset " + std::to_string (r.hasOffset) + " " + std::to_string (r.offsetSamples)
             + " timefix " + std::to_string (r.timeFixDb);
    }

    // Shortest signed distance between two angles in degrees.
    double angleDiff (double a, double b)
    {
        return std::remainder (a - b, 360.0);
    }
} // namespace

TEST_CASE ("Correction folds big turns into a flip plus at most 90 degrees", "[analyzer]")
{
    for (double deg : { 0.0, 45.0, 90.0, 91.0, 150.0, 180.0, -100.0, -179.0 })
    {
        const auto c = Correction::fromTotal (deg);
        INFO (deg);
        CHECK (std::abs (c.phaseDeg) <= 90.0 + 1e-9);
        CHECK_THAT (angleDiff (c.totalDeg(), deg), WithinAbs (0.0, 1e-9));
    }
}

TEST_CASE ("Analyzer undoes a phase rotation", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0);
    for (double deg : { 0.0, 30.0, -75.0, 120.0, 180.0 })
    {
        const auto r = align (ref, testutil::rotated (ref, deg), fs);
        INFO ("track turned " << deg << ": " << describe (r));
        CHECK (r.status == AlignStatus::ok);
        CHECK_THAT (angleDiff (r.correction.totalDeg(), -deg), WithinAbs (0.0, 1.0));
        CHECK (std::abs (r.correction.phaseDeg) <= 90.0);
        CHECK (r.confidence > 0.8f);
        CHECK_FALSE (r.hasOffset);
    }
}

TEST_CASE ("Analyzer flips polarity", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 4);
    const auto r = align (ref, testutil::scaled (ref, -0.4), fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK (r.correction.invert);
    CHECK_THAT (r.correction.phaseDeg, WithinAbs (0.0, 1.0));
    CHECK (r.lowEndGainDb > 6.f);   // was cancelling, now adds up
}

TEST_CASE ("Analyzer sees through a different tone and bleed", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0, 5);
    double rms = 0.0;
    for (float v : ref) rms += (double) v * v;
    rms = std::sqrt (rms / (double) ref.size());
    // Zero-phase colouring keeps the true turn at exactly 60 degrees.
    auto trk = testutil::Biquad::lowpass (1800.0, 0.7, fs).zeroPhase (testutil::rotated (ref, 60.0));
    trk = testutil::Biquad::peak (120.0, 1.0, 9.0, fs).zeroPhase (trk);
    trk = testutil::sum (trk, testutil::noise ((int) ref.size(), rms, 99));
    const auto r = align (ref, trk, fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK_THAT (angleDiff (r.correction.totalDeg(), -60.0), WithinAbs (0.0, 4.0));
}

TEST_CASE ("Analyzer turns a delayed track to help the low end, and reports the offset", "[analyzer]")
{
    // Kick in / kick out: the second mic 2.9 ms later. A rotation can't undo a
    // delay, but it can line up the low end, which is where it cancels.
    const double fs = 48000.0;
    const auto ref = testutil::kicks (fs, 6.0, 7);
    const double d = 0.0029 * fs;
    const auto trk = testutil::scaled (testutil::delayed (ref, d), -0.8);
    const auto r = align (ref, trk, fs);
    INFO (describe (r));
    CHECK (r.status == AlignStatus::ok);
    CHECK (r.lowEndGainDb > 1.f);
    REQUIRE (r.hasOffset);
    CHECK_THAT (r.offsetSamples, WithinAbs (d, 0.5));
    CHECK (r.timeFixDb > 0.f);

    // Measured on the audio itself: below 300 Hz the found turn sums within
    // half a dB of the best any turn can do (the hits are judged where they
    // sound, so it leans a little towards the attack).
    const auto lowSum = [&] (double deg) {
        const auto s = testutil::sum (ref, testutil::rotated (trk, deg));
        const auto lp = testutil::Biquad::lowpass (300.0, 0.7, fs);
        const auto low = lp.zeroPhase (lp.zeroPhase (s));
        double e = 0.0;
        for (float v : low) e += (double) v * v;
        return e;
    };
    double best = 0.0;
    for (double deg = -180.0; deg < 180.0; deg += 5.0) best = std::max (best, lowSum (deg));
    const double found = lowSum (r.correction.totalDeg());
    INFO ("found " << 10.0 * std::log10 (found / best) << " dB from the best turn, " << 10.0 * std::log10 (found / lowSum (0.0)) << " dB over none");
    CHECK (found > best * std::pow (10.0, -0.05));
    CHECK (found > 1.25 * lowSum (0.0));
}

TEST_CASE ("Analyzer finds a turn per hit when the track changes under the kick", "[analyzer]")
{
    // A kick on every beat; the track answers each kick turned by a
    // different angle, like a bass changing notes. One angle can't fit all.
    const double fs = 48000.0, beat = 0.5;
    const double turns[] = { 20.0, 110.0, -60.0, 160.0, 20.0, 110.0, -60.0, 160.0, 20.0, 110.0 };
    const int n = (int) (fs * beat * 10.5);
    Signal ref ((size_t) n, 0.f), trk ((size_t) n, 0.f);
    testutil::Rng rng (5);
    for (int k = 0; k < 10; ++k)
    {
        Signal one ((size_t) n, 0.f);
        testutil::addKick (one, (int) ((0.2 + k * beat) * fs), fs, 1.0, rng);
        ref = testutil::sum (ref, one);
        trk = testutil::sum (trk, testutil::rotated (one, turns[k]));
    }

    const auto r = align (ref, trk, fs);
    INFO (describe (r));
    REQUIRE (r.status == AlignStatus::ok);
    REQUIRE (r.hits.size() >= 8);
    CHECK (r.hitAgreement < 0.8f);
    for (const auto& h : r.hits)
    {
        const int k = (int) std::lround ((h.sample / fs - 0.2) / beat);
        REQUIRE (k >= 0);
        REQUIRE (k < 10);
        INFO ("hit " << k << " at " << h.sample / fs << " s: " << h.totalDeg << " deg, fit " << h.fit << ", +" << h.gainDb << " dB");
        CHECK_THAT (angleDiff (h.totalDeg, -turns[k]), WithinAbs (0.0, 3.0));
        CHECK (h.fit > 0.8f);
    }
}

TEST_CASE ("Analyzer says so when the signals are unrelated", "[analyzer]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 6.0, 11);
    double rms = 0.0;
    for (float v : ref) rms += (double) v * v;
    const auto r = align (ref, testutil::noise ((int) ref.size(), std::sqrt (rms / (double) ref.size()), 12), fs);
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
        const auto r = align (ref, testutil::rotated (ref, -45.0), fs);
        INFO (fs << " Hz: " << describe (r));
        CHECK (r.status == AlignStatus::ok);
        CHECK_THAT (angleDiff (r.correction.totalDeg(), 45.0), WithinAbs (0.0, 1.0));
    }
}

TEST_CASE ("Rotating with the result makes the track sum with the reference", "[analyzer][rotator]")
{
    const double fs = 48000.0;
    const auto ref = testutil::drums (fs, 5.0, 16);
    const auto trk = testutil::scaled (testutil::rotated (ref, 135.0), 0.8);
    const auto r = align (ref, trk, fs);
    REQUIRE (r.status == AlignStatus::ok);

    Rotator rot;
    rot.prepare (fs, 1);
    rot.setTarget (r.correction, false);
    Signal y (trk);
    for (size_t s = 0; s < y.size(); s += 512)
    {
        float* ch[] = { y.data() + s };
        rot.process (ch, 1, (int) std::min<size_t> (512, y.size() - s));
    }
    // The host delays the reference by the reported latency.
    const auto refLate = testutil::delayed (ref, rot.latency());
    CHECK (testutil::correlation (refLate.data(), y.data(), 9600, (int) y.size() - 9600) > 0.99);
}
