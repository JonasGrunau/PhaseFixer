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
// without touching the audio.
class Capture
{
public:
    static constexpr int kBlock = 256;

    void prepare (double sampleRate, double seconds = 12.0);
    void reset() noexcept;

    // Audio thread. `ref` may be null (no sidechain): it counts as silence.
    void push (const float* ref, const float* track, int n) noexcept;

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

    // Mean square of one block (absolute block index), reference or track.
    float blockEnergy (int64_t block, bool reference) const noexcept;
    int64_t blocksWritten() const noexcept { return written() / kBlock; }

private:
    double fs = 48000.0;
    int size = 0, mask = 0, blocks = 0, blockMask = 0;
    std::unique_ptr<std::atomic<float>[]> ref, trk, refBlock, trkBlock;
    std::atomic<int64_t> count { 0 };
    double accRef = 0.0, accTrk = 0.0;
    int accN = 0;
};

} // namespace pf
