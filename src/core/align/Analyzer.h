#pragma once

#include "Correction.h"

#include <cstdint>

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

struct AlignResult
{
    AlignStatus status = AlignStatus::notEnoughSignal;
    Correction correction;       // rotation + polarity for the track
    float confidence = 0.f;      // 0..1
    float coherence = 0.f;       // share of the two signals that is common, 0..1
    float lowEndGainDb = 0.f;    // reference + track summed over 20-300 Hz, corrected vs as it was
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
// 1. Frames (Hann, 4x the lag range long, 75 % overlap) count only where both
//    signals play; frames with a shared hit weigh more than sustained bleed,
//    and loudness is levelled so one loud bar doesn't decide alone.
// 2. Cross-spectrum S_ab and bias-corrected coherence per bin.
// 3. Rotation: the angle that maximises the summed energy is -arg of the
//    weighted sum of S_ab; weights favour the coherent part below ~1 kHz,
//    where PHA-979's manual asks for correlation near 1 (higher bands may
//    drift). Polarity takes the part beyond +-90 degrees.
// 4. Confidence: how well one angle fits (the weighted phase spread), the
//    coherence, whether ~1 s segments land on the same angle, and how much
//    material there was.
// 5. Time anchor: maximum-likelihood GCC (Knapp & Carter) finds how far
//    apart the two arrive, and how much a time move would still add.
AlignResult analyze (const float* ref, const float* track, int numSamples, const AnalyzerSettings& settings);

} // namespace pf
