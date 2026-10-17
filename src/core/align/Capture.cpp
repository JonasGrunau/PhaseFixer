#include "Capture.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pf
{
namespace
{
    constexpr float kActive = 1e-6f;   // -60 dBFS mean square
    constexpr int kSafety = 16384;     // the writer may run this far ahead of count
    constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();

    int pow2AtLeast (int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }
} // namespace

void Capture::prepare (double sampleRate, double seconds)
{
    fs = sampleRate;
    size = pow2AtLeast ((int) std::ceil (seconds * fs));
    mask = size - 1;
    blocks = size / kBlock;
    blockMask = blocks - 1;
    ref.reset (new std::atomic<float>[(size_t) size]);
    trk.reset (new std::atomic<float>[(size_t) size]);
    refBlock.reset (new std::atomic<float>[(size_t) blocks]);
    trkBlock.reset (new std::atomic<float>[(size_t) blocks]);
    blockPpq.reset (new std::atomic<double>[(size_t) blocks]);
    blockRate.reset (new std::atomic<double>[(size_t) blocks]);
    reset();
}

void Capture::reset() noexcept
{
    for (int i = 0; i < size; ++i)
    {
        ref[(size_t) i].store (0.f, std::memory_order_relaxed);
        trk[(size_t) i].store (0.f, std::memory_order_relaxed);
    }
    for (int i = 0; i < blocks; ++i)
    {
        refBlock[(size_t) i].store (0.f, std::memory_order_relaxed);
        trkBlock[(size_t) i].store (0.f, std::memory_order_relaxed);
        blockPpq[(size_t) i].store (kUnknown, std::memory_order_relaxed);
        blockRate[(size_t) i].store (0.0, std::memory_order_relaxed);
    }
    restart();
}

void Capture::restart() noexcept
{
    accRef = accTrk = 0.0;
    accN = 0;
    tlPpq = accPpq = kUnknown;
    tlRate = 0.0;
    tlIndex = 0;
    count.store (0, std::memory_order_release);
}

void Capture::setTimeline (double ppqOfNext, double ppqPerSample) noexcept
{
    tlPpq = ppqOfNext;
    tlRate = ppqPerSample;
    tlIndex = count.load (std::memory_order_relaxed);
}

void Capture::push (const float* r, const float* t, int n) noexcept
{
    if (size == 0) return;
    int64_t pos = count.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++pos)
    {
        const float a = r != nullptr ? r[i] : 0.f, b = t[i];
        if (accN == 0)
            accPpq = tlPpq + (double) (pos - tlIndex) * tlRate;   // NaN stays NaN
        ref[(size_t) (pos & mask)].store (a, std::memory_order_relaxed);
        trk[(size_t) (pos & mask)].store (b, std::memory_order_relaxed);
        accRef += (double) a * a;
        accTrk += (double) b * b;
        if (++accN == kBlock)
        {
            const int64_t block = pos / kBlock;
            refBlock[(size_t) (block & blockMask)].store ((float) (accRef / kBlock), std::memory_order_relaxed);
            trkBlock[(size_t) (block & blockMask)].store ((float) (accTrk / kBlock), std::memory_order_relaxed);
            blockPpq[(size_t) (block & blockMask)].store (accPpq, std::memory_order_relaxed);
            blockRate[(size_t) (block & blockMask)].store (tlRate, std::memory_order_relaxed);
            accRef = accTrk = 0.0;
            accN = 0;
        }
        // Publish every block, so readers see the samples and energies together.
        if (accN == 0)
            count.store (pos + 1, std::memory_order_release);
    }
    count.store (pos, std::memory_order_release);
}

bool Capture::copy (int64_t start, int n, float* r, float* t) const noexcept
{
    const int64_t end = written();
    if (start < 0 || start + n > end || start < end - size + kSafety) return false;
    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((start + i) & mask);
        if (r != nullptr) r[i] = ref[idx].load (std::memory_order_relaxed);
        if (t != nullptr) t[i] = trk[idx].load (std::memory_order_relaxed);
    }
    // The writer may have lapped the start while we were copying.
    return start >= written() - size + kSafety;
}

double Capture::timelineAt (int64_t index) const noexcept
{
    const int64_t block = index / kBlock;
    if (index < 0 || block >= blocksWritten() || block < blocksWritten() - blocks + kSafety / kBlock)
        return kUnknown;
    const double p = blockPpq[(size_t) (block & blockMask)].load (std::memory_order_relaxed);
    return p + (double) (index - block * kBlock) * blockRate[(size_t) (block & blockMask)].load (std::memory_order_relaxed);
}

float Capture::blockEnergy (int64_t block, bool reference) const noexcept
{
    const auto& a = reference ? refBlock : trkBlock;
    return a[(size_t) (block & blockMask)].load (std::memory_order_relaxed);
}

Capture::Activity Capture::activity (double windowSeconds) const noexcept
{
    Activity a;
    const int64_t last = blocksWritten();
    const int64_t n = std::min<int64_t> ((int64_t) (windowSeconds * fs / kBlock), std::min<int64_t> (last, blocks - 1));
    const double secs = kBlock / fs;
    for (int64_t b = last - n; b < last; ++b)
    {
        const bool r = blockEnergy (b, true) > kActive, t = blockEnergy (b, false) > kActive;
        if (r) a.ref += secs;
        if (t) a.track += secs;
        if (r && t) a.shared += secs;
    }
    return a;
}

int64_t Capture::strongestHit (double windowSeconds, int minAgeSamples, float& scoreDb) const noexcept
{
    constexpr int kHistory = 8;
    const int64_t last = blocksWritten() - (minAgeSamples + kBlock - 1) / kBlock;
    const int64_t n = std::min<int64_t> ((int64_t) (windowSeconds * fs / kBlock), std::min<int64_t> (last, blocks - 1 - kHistory));
    int64_t best = -1;
    scoreDb = 0.f;
    for (int64_t b = std::max<int64_t> (last - n, kHistory); b < last; ++b)
    {
        const float er = blockEnergy (b, true), et = blockEnergy (b, false);
        if (er < kActive || et < kActive) continue;
        float pr = 0.f, pt = 0.f;
        for (int k = 1; k <= kHistory; ++k)
        {
            pr += blockEnergy (b - k, true);
            pt += blockEnergy (b - k, false);
        }
        const float riseR = 10.f * std::log10 ((er + 1e-10f) / (pr / kHistory + 1e-10f));
        const float riseT = 10.f * std::log10 ((et + 1e-10f) / (pt / kHistory + 1e-10f));
        // Louder hits win ties: add a little of the level itself.
        const float s = std::min (riseR, riseT) + 0.1f * 10.f * std::log10 (std::min (er, et));
        if (std::min (riseR, riseT) > 3.f && (best < 0 || s > scoreDb))
        {
            best = b * kBlock;
            scoreDb = s;
        }
    }
    return best;
}

} // namespace pf
