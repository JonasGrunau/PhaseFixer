#pragma once

#include <vector>

namespace pf
{

// One turn per hit of the reference, placed on the host's timeline (quarter
// notes): for material where one angle can't fit every hit, like a bass that
// changes notes under the kick.
//
// Between two hits the earlier turn holds while its hit rings; the move to
// the next turn takes the second half of the gap (kMinGlideMs..kMaxGlideMs,
// raised-cosine shaped, so the pitch hardly bends) and lands kLandMs before
// the next hit.
struct MapPoint
{
    double ppq = 0.0;          // where the hit starts
    double deg = 0.0;          // total turn from this hit on, flip included, -180..180
    double learnedDeg = 0.0;   // what ALIGN found; deg differs once edited by hand
    float gainDb = 0.f;        // what the learned turn adds to this hit's sum
    float fit = 0.f;           // 0..1, how clearly one angle won for this hit
    bool edited = false;
};

class PhaseMap
{
public:
    static constexpr double kMinGlideMs = 20.0, kMaxGlideMs = 250.0, kLandMs = 5.0;
    static constexpr double kSameHitMs = 30.0;   // hits closer than this are the same hit

    std::vector<MapPoint> points;   // sorted by ppq

    bool empty() const noexcept { return points.empty(); }
    int size() const noexcept { return (int) points.size(); }

    // Last point at or before ppq, -1 before the first.
    int indexAt (double ppq) const noexcept;
    // The turn at a timeline position, transitions included. With the host
    // looping [loopStart, loopEnd) the map runs round the loop: after its last
    // hit comes its first one again, with the same glide as between any two.
    double angleAt (double ppq, double bpm, double loopStart = 0.0, double loopEnd = 0.0) const noexcept;

    // What ALIGN found over the stretch [fromPpq, toPpq] of the timeline. The
    // learned points in that stretch are replaced; points edited by hand stay
    // (their learned value is updated). Hits heard more than once, as in a
    // loop, are merged.
    struct Hit
    {
        double ppq = 0.0, deg = 0.0;
        float gainDb = 0.f, fit = 0.f;
    };
    void learn (std::vector<Hit> hits, double fromPpq, double toPpq, double bpm);

    // Sets one point's turn by hand; setting it back to the learned value
    // clears the edit.
    void edit (int index, double deg);
};

// Wrapped to (-180, 180].
double wrapDeg (double deg) noexcept;

// The timeline position `quarters` before `ppq` as it was played. With the
// host looping [loopStart, loopEnd), stepping back past the loop start lands
// in the previous pass, at the loop's end.
double timelineBefore (double ppq, double quarters, double loopStart = 0.0, double loopEnd = 0.0) noexcept;

} // namespace pf
