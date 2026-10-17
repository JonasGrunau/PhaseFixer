#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace pf
{

// The last few seconds of the reference and the track (mono, before any
// correction), written by the audio thread and read by the analysis worker
// and the editor. Sample positions are absolute: sample i of the stream ever
// pushed. Values are relaxed atomics, so readers never block the writer; a
// reader copying a region the writer is overwriting gets a mix of old and new
// samples, which copy() reports by returning false.
//
// Alongside the samples it keeps the energy of every kBlock samples, which is
// enough to tell how much shared signal there is and where the hits are
// without touching the audio, and where each block sat on the host's
// timeline (quarter notes), so hits can be placed in the song. A block the
// timeline jumps inside keeps where it jumped to as well: a loop wrapping
// between two host blocks, or inside one (hosts that don't split their
// blocks at the loop's end report only where each block starts).
class Capture
{
public:
    static constexpr int kBlock = 256;

    void prepare (double sampleRate, double seconds = 12.0);
    void reset() noexcept;
    // Forgets everything so far without clearing memory (audio thread):
    // readers only ever look at what was written since.
    void restart() noexcept;

    // Audio thread. `ref` may be null (no sidechain): it counts as silence.
    void push (const float* ref, const float* track, int n) noexcept;
    // Audio thread, before push(): the timeline position of the next sample
    // and how far it moves per sample. NaN while the transport stands. With
    // the host's loop, a wrap inside the coming samples is placed right.
    void setTimeline (double ppqOfNext, double ppqPerSample, double loopStart = 0.0, double loopEnd = 0.0) noexcept;

    double sampleRate() const noexcept { return fs; }
    int capacity() const noexcept { return size; }
    int64_t written() const noexcept { return count.load (std::memory_order_acquire); }

    // Copies samples [start, start + n). False when part of it is not in the
    // buffer (too old, not written yet, or overwritten during the copy).
    bool copy (int64_t start, int n, float* ref, float* track) const noexcept;

    // Seconds of signal within the last `window` seconds where the reference,
    // the track, and both at once are above -60 dBFS.
    struct Activity { double ref = 0.0, track = 0.0, shared = 0.0; };
    Activity activity (double windowSeconds) const noexcept;

    // The strongest hit both signals share (sharpest rise in level, in dB)
    // within the last `window` seconds, or -1. `minAgeSamples` skips hits too
    // recent to have their tail captured yet.
    int64_t strongestHit (double windowSeconds, int minAgeSamples, float& scoreDb) const noexcept;

    // Timeline position of a captured sample; NaN when it is unknown (the
    // transport stood) or no longer in the buffer.
    double timelineAt (int64_t index) const noexcept;

    // Mean square of one block (absolute block index), reference or track.
    float blockEnergy (int64_t block, bool reference) const noexcept;
    int64_t blocksWritten() const noexcept { return written() / kBlock; }

private:
    double fs = 48000.0;
    int size = 0, mask = 0, blocks = 0, blockMask = 0;
    std::unique_ptr<std::atomic<float>[]> ref, trk, refBlock, trkBlock;
    std::unique_ptr<std::atomic<double>[]> blockPpq, blockRate, blockJumpPpq;
    std::unique_ptr<std::atomic<int>[]> blockJumpAt;   // sample in the block where the timeline jumped, -1: none
    std::atomic<int64_t> count { 0 };
    double accRef = 0.0, accTrk = 0.0;
    int accN = 0;
    double tlPpq = 0.0, tlRate = 0.0, accPpq = 0.0;   // audio thread
    int64_t tlIndex = 0, wrapIndex = -1;   // wrapIndex: where the loop comes round, -1: not in these samples
    double loopLength = 0.0;
    int accJumpAt = -1;
    double accJumpPpq = 0.0;
};

// Samples from a point of the timeline (ppq) until the host's loop comes
// round, or -1 when it isn't inside the loop (or nothing loops).
int64_t samplesToLoopEnd (double ppq, double ppqPerSample, double loopStart, double loopEnd) noexcept;

} // namespace pf
