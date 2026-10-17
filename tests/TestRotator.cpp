#include "TestUtil.h"
#include "align/Capture.h"
#include "align/Rotator.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;
using Catch::Matchers::WithinAbs;

namespace
{
    // Runs stereo `x` (same signal both sides) through a rotator in blocks of
    // `block` with one target throughout; returns the left output.
    testutil::Signal run (Rotator& r, const testutil::Signal& x, int block = 300)
    {
        testutil::Signal l (x), rr (x);
        for (size_t s = 0; s < l.size(); s += (size_t) block)
        {
            float* ch[] = { l.data() + s, rr.data() + s };
            r.process (ch, 2, (int) std::min<size_t> ((size_t) block, l.size() - s));
        }
        return l;
    }
} // namespace

TEST_CASE ("Rotator at 0 degrees is a pure delay of its reported latency", "[rotator]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        Rotator r;
        r.prepare (fs, 2);
        r.setTarget ({}, false);
        const auto x = testutil::noise ((int) (0.4 * fs), 0.3, 7);
        const auto y = run (r, x);
        const int lat = r.latency();
        CHECK (lat == Rotator::latencyFor (fs));
        CHECK (lat * 1000.0 / fs < 50.0);
        for (int i = lat; i < (int) y.size(); ++i)
            REQUIRE (y[(size_t) i] == x[(size_t) (i - lat)]);
    }
}

TEST_CASE ("Rotator turns every frequency by the same angle", "[rotator]")
{
    const double fs = 48000.0;
    for (double deg : { 37.0, 90.0, -120.0 })
        for (double hz : { 40.0, 200.0, 3000.0, 15000.0 })
        {
            Rotator r;
            r.prepare (fs, 2);
            Correction c;
            c.phaseDeg = deg;
            r.setTarget (c, false);
            const int n = (int) (0.6 * fs);
            testutil::Signal x ((size_t) n);
            for (int i = 0; i < n; ++i) x[(size_t) i] = (float) std::cos (2.0 * std::numbers::pi * hz * i / fs);
            const auto y = run (r, x);
            const int lat = r.latency();
            double err = 0.0, ref = 0.0;
            for (int i = lat + 4800; i < n - 4800; ++i)
            {
                const double want = std::cos (2.0 * std::numbers::pi * hz * (i - lat) / fs + deg * std::numbers::pi / 180.0);
                err += (y[(size_t) i] - want) * (y[(size_t) i] - want);
                ref += want * want;
            }
            INFO (deg << " deg @ " << hz << " Hz: error " << 10.0 * std::log10 (err / ref) << " dB");
            CHECK (10.0 * std::log10 (err / ref) < -40.0);
        }
}

TEST_CASE ("Rotator: 180 degrees is a polarity flip, and the flip key is exact", "[rotator]")
{
    const double fs = 48000.0;
    const auto x = testutil::drums (fs, 0.5);
    Rotator a, b;
    a.prepare (fs, 2);
    b.prepare (fs, 2);
    Correction flip;
    flip.invert = true;
    a.setTarget (flip, false);
    Correction half;
    half.phaseDeg = 180.0;
    b.setTarget (half, false);
    const auto ya = run (a, x), yb = run (b, x);
    const int lat = a.latency();
    for (int i = lat; i < (int) x.size(); ++i)
        REQUIRE (ya[(size_t) i] == -x[(size_t) (i - lat)]);
    double worst = 0.0;
    for (int i = lat; i < (int) x.size(); ++i) worst = std::max (worst, (double) std::abs (yb[(size_t) i] - ya[(size_t) i]));
    CHECK (worst < 1e-6);
}

TEST_CASE ("Rotator changes don't click", "[rotator]")
{
    const double fs = 48000.0, hz = 110.0;
    const int n = 48000;
    testutil::Signal x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * std::numbers::pi * hz * i / fs));
    const double maxStep = 0.5 * 2.0 * std::numbers::pi * hz / fs;

    Rotator r;
    r.prepare (fs, 1);
    r.setTarget ({}, false);
    testutil::Signal y (x);
    double worst = 0.0;
    float last = 0.f;
    for (int start = 0; start < n; start += 128)
    {
        Correction c;
        if (start >= 12000) c.phaseDeg = 140.0;
        if (start >= 24000) c.invert = true;
        if (start >= 32000) c.phaseDeg = -60.0;
        r.setTarget (c, start >= 40000);   // and A/B
        float* ch[] = { y.data() + start };
        r.process (ch, 1, 128);
        for (int i = start; i < start + 128; ++i)
        {
            if (i > r.latency() + 100) worst = std::max (worst, (double) std::abs (y[(size_t) i] - last));
            last = y[(size_t) i];
        }
    }
    CHECK (worst < 1.6 * maxStep);
}

TEST_CASE ("Rotator turns along the arc: the level holds through a big turn", "[rotator]")
{
    const double fs = 48000.0, hz = 110.0;
    const int n = 24000, period = (int) std::lround (fs / hz);
    testutil::Signal x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) std::sin (2.0 * std::numbers::pi * hz * i / fs);

    Rotator r;
    r.prepare (fs, 1);
    r.setTarget (0.0, false);
    testutil::Signal y (x);
    for (int start = 0; start < n; start += 64)
    {
        r.setTarget (start >= 9000 ? 170.0 : 0.0, false);
        float* ch[] = { y.data() + start };
        r.process (ch, 1, std::min (64, n - start));
    }
    // RMS over every period from well after the start. A cut straight through
    // the circle would dip ~21 dB in the middle of the turn; what is left is
    // the window not fitting the briefly faster wave.
    double lo = 1e9, hi = 0.0;
    for (int s = r.latency() + 4800; s + period < n; s += period / 4)
    {
        double e = 0.0;
        for (int i = s; i < s + period; ++i) e += (double) y[(size_t) i] * y[(size_t) i];
        const double rms = std::sqrt (e / period);
        lo = std::min (lo, rms);
        hi = std::max (hi, rms);
    }
    INFO ("rms " << lo << " .. " << hi);
    CHECK (20.0 * std::log10 (hi / lo) < 1.5);
}

TEST_CASE ("Offline render plays what the real-time chain played, without the latency", "[rotator]")
{
    const double fs = 48000.0;
    const auto x = testutil::drums (fs, 1.5, 21);
    const int n = (int) x.size();
    std::vector<Turn> turns { { 0, 30.0 }, { 20000, -120.0 }, { 20480, -110.0 }, { 51000, 175.0 } };

    // Real time: targets at block starts, in odd block sizes.
    Rotator r;
    r.prepare (fs, 2);
    testutil::Signal l (x), rr (testutil::scaled (x, 0.5));
    l.resize ((size_t) n + 4096, 0.f);
    rr.resize ((size_t) n + 4096, 0.f);
    size_t next = 0;
    for (int s = 0; s < n + 4096;)
    {
        while (next < turns.size() && turns[next].sample <= s) r.setTarget (turns[next++].deg, false);
        int len = std::min (300, n + 4096 - s);
        if (next < turns.size()) len = std::min (len, turns[next].sample - s);
        float* ch[] = { l.data() + s, rr.data() + s };
        r.process (ch, 2, len);
        s += len;
    }

    const auto half = testutil::scaled (x, 0.5);
    const float* in[] = { x.data(), half.data() };
    testutil::Signal ol ((size_t) n), orr ((size_t) n);
    float* out[] = { ol.data(), orr.data() };
    renderOffline (in, out, 2, n, fs, turns, false);

    const int lat = r.latency();
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        worst = std::max (worst, (double) std::abs (ol[(size_t) i] - l[(size_t) (i + lat)]));
        worst = std::max (worst, (double) std::abs (orr[(size_t) i] - rr[(size_t) (i + lat)]));
    }
    CHECK (worst < 1e-5);
}

TEST_CASE ("Offline render of a loop pass joins its end to its start", "[rotator]")
{
    const double fs = 48000.0;
    const auto loop = testutil::drums (fs, 1.0, 22);
    const int n = (int) loop.size();
    std::vector<Turn> turns { { 0, 60.0 }, { n / 2, -40.0 } };

    // The loop played three times in real time, turns repeating each pass.
    Rotator r;
    r.prepare (fs, 1);
    testutil::Signal y;
    for (int pass = 0; pass < 3; ++pass) y.insert (y.end(), loop.begin(), loop.end());
    for (int pass = 0; pass < 3; ++pass)
        for (int s = 0; s < n;)
        {
            for (const auto& t : turns)
                if (t.sample == s) r.setTarget (t.deg, false);
            const int len = std::min (256, (s < n / 2 ? n / 2 : n) - s);
            float* ch[] = { y.data() + pass * n + s };
            r.process (ch, 1, len);
            s += len;
        }

    const float* in[] = { loop.data() };
    testutil::Signal o ((size_t) n);
    float* out[] = { o.data() };
    renderOffline (in, out, 1, n, fs, turns, true);

    // The middle pass, which has the loop on both sides like the render.
    const int lat = r.latency();
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
        worst = std::max (worst, (double) std::abs (o[(size_t) i] - y[(size_t) (n + i + lat)]));
    CHECK (worst < 1e-4);
}

TEST_CASE ("Offline rotation matches the real-time chain", "[rotator]")
{
    const double fs = 44100.0;
    const auto x = testutil::drums (fs, 1.0, 9);
    Correction c;
    c.phaseDeg = -73.0;
    c.invert = true;

    Rotator r;
    r.prepare (fs, 2);
    r.setTarget (c, false);
    const auto rt = run (r, x);

    const auto h = hilbertOffline (x.data(), (int) x.size(), fs);
    testutil::Signal off (x.size());
    rotateOffline (x.data(), h.data(), (int) x.size(), c, off.data());

    const int lat = r.latency();
    double worst = 0.0;
    for (int i = 4000; i < (int) x.size() - lat - 4000; ++i)
        worst = std::max (worst, (double) std::abs (rt[(size_t) (i + lat)] - off[(size_t) i]));
    CHECK (worst < 1e-4);
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
    CHECK (cap.activity (3.0).shared > 2.0);

    std::vector<float> r (1000), t (1000);
    REQUIRE (cap.copy (100000, 1000, r.data(), t.data()));
    for (int i = 0; i < 1000; ++i)
    {
        REQUIRE (r[(size_t) i] == ref[(size_t) (100000 + i)]);
        REQUIRE (t[(size_t) i] == trk[(size_t) (100000 + i)]);
    }
    CHECK_FALSE (cap.copy ((int64_t) ref.size() - 10, 100, r.data(), t.data()));

    float score = 0.f;
    CHECK (cap.strongestHit (2.0, 0, score) >= 0);
    CHECK (score > 3.f);
}

TEST_CASE ("Capture places what it heard on the host's timeline", "[capture]")
{
    const double fs = 48000.0, perSample = 2.0 / fs;   // 120 BPM
    Capture cap;
    cap.prepare (fs, 4.0);
    const auto x = testutil::noise (48000, 0.1, 3);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    int64_t at = 0;
    for (int s = 0; s < 48000; s += 441, at += 441)
    {
        // Playing from beat 8 for the first half second, then stopped.
        cap.setTimeline (s < 24000 ? 8.0 + (double) s * perSample : nan, perSample);
        cap.push (x.data() + s, x.data() + s, std::min (441, 48000 - s));
    }
    CHECK_THAT (cap.timelineAt (0), WithinAbs (8.0, 1e-9));
    CHECK_THAT (cap.timelineAt (12345), WithinAbs (8.0 + 12345 * perSample, 1e-9));
    CHECK (std::isnan (cap.timelineAt (40000)));
    CHECK (std::isnan (cap.timelineAt (60000)));   // not written yet
}
