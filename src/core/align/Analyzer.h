#pragma once

#include "Correction.h"

#include <cstdint>
#include <vector>

namespace pf
{

enum class AlignStatus
{
    ok,
    notEnoughSignal,   // too little time where both play
    noReference,       // the sidechain is silent
    noTrack,           // the track is silent
    unrelated          // both play, but they don't share a sound
};

// One hit of the reference (an onset below ~400 Hz: a kick, a bass note, a
// snare's body) and the turn that sums the track best from it to the next.
struct HitAngle
{
    int64_t sample = 0;        // onset, index into the analysed buffers
    double totalDeg = 0.0;     // turn for this hit, flip included
    float fit = 0.f;           // 0..1, how clearly one angle wins here
    float gainDb = 0.f;        // what that turn adds to this hit's sum
};

struct AlignResult
{
    AlignStatus status = AlignStatus::notEnoughSignal;
    Correction correction;       // rotation + polarity for the track
    float confidence = 0.f;      // 0..1
    float coherence = 0.f;       // share of the two signals that is common, 0..1
    float lowEndGainDb = 0.f;    // reference + track summed in the low end, corrected vs as it was (with hits: where they meet)
    float sumGainDb = 0.f;       // the same over 20 Hz-20 kHz

    // The time anchor. The track is never moved, but how far apart the two
    // arrive tells how much a rotation can do: the further apart, the more
    // the phase difference changes with frequency, and one angle fits only
    // part of the spectrum. timeFixDb is what moving the track by the offset
    // (in the arrangement) would add on top, full band.
    bool hasOffset = false;
    double offsetSamples = 0.0;  // positive: the track arrives later
    float timeFixDb = 0.f;

    double activeSeconds = 0.0;  // shared signal that went into the result
    int64_t hitSample = -1;      // strongest shared hit, as an index into the analysed buffers

    std::vector<HitAngle> hits;  // per hit, in time order (empty for material without hits)
    float hitAgreement = 1.f;    // 0..1, whether the hits want the same turn
    float hitConfidence = 0.f;   // 0..1, confidence in the turns per hit
};

struct AnalyzerSettings
{
    double sampleRate = 48000.0;
    double maxLagMs = limits::kMaxLagMs;
    double minSeconds = 0.5;     // less shared signal than this: notEnoughSignal
};

// Finds the phase rotation (and polarity) that makes `track` sum best with
// `ref`, without moving it in time. Both buffers cover the same time span.
//
// 1. Hits: onsets of the reference below ~400 Hz. Each hit is judged from
//    just before it to the next one (at most 0.6 s), through the same Hilbert
//    filter the plugin plays with, on what is heard there: the low end below
//    ~150 Hz (weight), 150-500 Hz over the first 45 ms (punch), and a little
//    of 500 Hz-2 kHz. That gives a turn per hit, and, hits levelled against
//    each other, one turn for all of them.
// 2. Material without hits (pads, sustained parts): frames (Hann, 4x the lag
//    range long, 75 % overlap) where both play, and the angle that maximises
//    the summed energy, -arg of the weighted sum of S_ab, weighted towards
//    the coherent part below ~1 kHz, where PHA-979's manual asks for
//    correlation near 1. Polarity takes the part beyond +-90 degrees.
// 3. Confidence: how well one angle fits, the coherence, whether the hits
//    (or ~1 s segments) want the same angle, and how much material there was.
// 4. Time anchor: maximum-likelihood GCC (Knapp & Carter) finds how far
//    apart the two arrive, and how much a time move would still add.
AlignResult analyze (const float* ref, const float* track, int numSamples, const AnalyzerSettings& settings);

} // namespace pf
