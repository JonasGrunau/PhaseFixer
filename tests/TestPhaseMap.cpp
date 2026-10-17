#include "align/PhaseMap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;
using Catch::Matchers::WithinAbs;

namespace
{
    PhaseMap fourHits()
    {
        // Kicks on every beat of bar 1 at 120 BPM (a beat is 500 ms).
        PhaseMap m;
        m.learn ({ { 0.0, 10.0, 3.f, 0.9f }, { 1.0, 100.0, 2.f, 0.9f }, { 2.0, -170.0, 4.f, 0.9f }, { 3.0, 170.0, 1.f, 0.9f } },
                 0.0, 4.0, 120.0);
        return m;
    }
} // namespace

TEST_CASE ("Phase map holds each hit's turn while it rings, then glides to the next", "[map]")
{
    const auto m = fourHits();
    REQUIRE (m.size() == 4);
    const double bpm = 120.0, ms = bpm / 60000.0;   // ppq per millisecond

    CHECK_THAT (m.angleAt (-1.0, bpm), WithinAbs (10.0, 1e-9));          // before the first hit
    CHECK_THAT (m.angleAt (0.0, bpm), WithinAbs (10.0, 1e-9));
    CHECK_THAT (m.angleAt (0.4, bpm), WithinAbs (10.0, 1e-9));           // 200 ms in: still the first hit
    CHECK_THAT (m.angleAt (1.0 - 5.0 * ms, bpm), WithinAbs (100.0, 1e-9));   // landed 5 ms early
    CHECK_THAT (m.angleAt (1.0, bpm), WithinAbs (100.0, 1e-9));
    CHECK_THAT (m.angleAt (9.0, bpm), WithinAbs (170.0, 1e-9));          // after the last: holds

    // Halfway through a glide: halfway between, along the shorter arc
    // (-170 to 170 goes through 180, not through 0).
    const double land = 3.0 - 5.0 * ms, start = land - 250.0 * ms;
    CHECK_THAT (std::abs (m.angleAt (0.5 * (start + land), bpm)), WithinAbs (180.0, 1e-6));

    // Monotonic and continuous over the glide from 10 to 100 degrees.
    double last = m.angleAt (0.0, bpm);
    for (double p = 0.0; p <= 1.0; p += 0.001)
    {
        const double a = m.angleAt (p, bpm);
        REQUIRE (a >= last - 1e-9);
        REQUIRE (a - last < 2.0);
        last = a;
    }
}

TEST_CASE ("Phase map merges loop passes and keeps hand edits", "[map]")
{
    auto m = fourHits();
    m.edit (1, 45.0);
    CHECK (m.points[1].edited);
    CHECK_THAT (m.points[1].deg, WithinAbs (45.0, 1e-9));

    // The bar again: beat 1 heard twice (two loop passes, a hair apart),
    // beat 2 is the edited one, and a new hit on beat 4.5.
    m.learn ({ { 0.002, 20.0, 3.f, 0.8f }, { 0.0, 40.0, 3.f, 0.8f },
               { 1.001, 120.0, 2.f, 0.8f }, { 2.0, -160.0, 2.f, 0.8f }, { 3.0, 160.0, 2.f, 0.8f },
               { 3.5, -30.0, 1.f, 0.8f } },
             0.0, 3.9, 120.0);

    REQUIRE (m.size() == 5);   // beats 1-4 plus the new one
    CHECK_THAT (m.points[0].deg, WithinAbs (30.0, 1e-6));        // the two passes, averaged
    CHECK_THAT (m.points[1].deg, WithinAbs (45.0, 1e-9));        // the edit stays
    CHECK_THAT (m.points[1].learnedDeg, WithinAbs (120.0, 1e-9));
    CHECK_THAT (m.points[4].ppq, WithinAbs (3.5, 1e-9));

    // Points outside what was heard stay untouched.
    m.learn ({ { 8.0, 5.0, 1.f, 0.9f } }, 7.5, 8.5, 120.0);
    CHECK (m.size() == 6);

    // Setting an edit back to the learned value clears it.
    m.edit (1, 120.0);
    CHECK_FALSE (m.points[1].edited);
}
