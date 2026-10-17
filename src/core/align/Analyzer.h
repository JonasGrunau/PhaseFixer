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
    unrelated,         // both play, but they don't share a sound
    outOfRange         // the best match sits at the edge of the search range
};

struct AlignResult
{
    AlignStatus status = AlignStatus::notEnoughSignal;
    Correction correction;       // what to apply to the track
    double lagSamples = 0.0;     // how late the track arrived (negative: early)
    float confidence = 0.f;      // 0..1
    float coherence = 0.f;       // share of the two signals that is common, 0..1
    float lowEndGainDb = 0.f;    // reference + track summed over 20-300 Hz, corrected vs as it was
    float sumGainDb = 0.f;       // the same over 20 Hz-20 kHz
    float rotateGainDb = 0.f;    // what the allpass adds on top of shift + polarity (0 when off)
    double activeSeconds = 0.0;  // shared signal that went into the result
    int64_t hitSample = -1;      // strongest shared hit, as an index into the analysed buffers
};

struct AnalyzerSettings
{
    double sampleRate = 48000.0;
    double maxLagMs = limits::kMaxShiftMs;
    double minSeconds = 0.5;     // less shared signal than this: notEnoughSignal
    bool fitRotation = true;
};

// Finds the shift, polarity and (if it helps audibly) allpass that make
// `track` sum best with `ref`. Both buffers cover the same time span.
//
// 1. Frames (Hann, 4x the search range long, 75 % overlap) count only where
//    both signals play; frames with a shared hit weigh more than sustained
//    bleed, and loudness is levelled so one loud bar doesn't decide alone.
// 2. Maximum-likelihood GCC (Knapp & Carter): the cross-spectrum weighted by
//    coherence^2 / (1 - coherence^2) / |S_ab|, band-limited to 30 Hz-16 kHz.
//    Sharp peaks even when the two mics sound different.
// 3. The strongest peaks are candidates. Each is refined to a fraction of a
//    sample, then scored by how loud reference + shifted track actually sum
//    (weighted to 60 Hz-2 kHz, where cancellation is heard), which also sets
//    the polarity. This avoids locking onto a reflection.
// 4. Second pass with the track frames pre-shifted by the winner, so long
//    delays don't lose coherence to the window, for the final fine shift,
//    the coherence and the confidence (peak sharpness, coherence, and how
//    many ~1 s segments agree on their own).
// 5. Phase rotation: the cross-correlation is recomputed through a grid of
//    2nd-order allpasses (frequency x Q), the best refined; kept only when it
//    raises the summed level by at least 0.3 dB.
AlignResult analyze (const float* ref, const float* track, int numSamples, const AnalyzerSettings& settings);

} // namespace pf
