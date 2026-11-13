// Runs the analyzer on two WAV files and prints the rotation the plugin would apply.
//   phasefixer_align <reference.wav> <track.wav> [max-lag-ms]
// Both files are mixed to mono and must share a sample rate. Reads 16/24/32-bit
// PCM and 32-bit float. No JUCE, so it builds with the core.

#include "align/Analyzer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    bool readWav (const char* path, std::vector<float>& mono, double& fs)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f) return false;
        std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
        const auto u16 = [&] (size_t p) { return (uint32_t) (uint8_t) d[p] | (uint32_t) (uint8_t) d[p + 1] << 8; };
        const auto u32 = [&] (size_t p) { return u16 (p) | u16 (p + 2) << 16; };
        if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0) return false;

        int format = 0, channels = 0, bits = 0;
        for (size_t p = 12; p + 8 <= d.size();)
        {
            const uint32_t len = u32 (p + 4);
            const size_t body = p + 8;
            if (std::memcmp (d.data() + p, "fmt ", 4) == 0)
            {
                format = (int) u16 (body);
                channels = (int) u16 (body + 2);
                fs = u32 (body + 4);
                bits = (int) u16 (body + 14);
                if (format == 0xFFFE && len >= 26) format = (int) u16 (body + 24);   // extensible
            }
            else if (std::memcmp (d.data() + p, "data", 4) == 0 && channels > 0)
            {
                const int bytes = bits / 8;
                if (bytes < 2 || bytes > 4) return false;   // 8-bit and odd sizes aren't read
                const size_t frames = std::min<size_t> (len, d.size() - body) / (size_t) (bytes * channels);
                mono.assign (frames, 0.f);
                for (size_t i = 0; i < frames; ++i)
                    for (int c = 0; c < channels; ++c)
                    {
                        const size_t q = body + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
                        float v = 0.f;
                        if (format == 3 && bits == 32) { uint32_t u = u32 (q); std::memcpy (&v, &u, 4); }
                        else if (bits == 16) v = (float) (int16_t) u16 (q) / 32768.f;
                        else if (bits == 24) v = (float) ((int32_t) (u16 (q) << 8 | (uint32_t) (uint8_t) d[q + 2] << 24) >> 8) / 8388608.f;
                        else if (bits == 32) v = (float) (int32_t) u32 (q) / 2147483648.f;
                        mono[i] += v / (float) channels;
                    }
                return true;
            }
            p = body + len + (len & 1);
        }
        return false;
    }

    const char* statusText (pf::AlignStatus s)
    {
        switch (s)
        {
            case pf::AlignStatus::ok: return "ok";
            case pf::AlignStatus::notEnoughSignal: return "not enough shared signal";
            case pf::AlignStatus::noReference: return "reference is silent";
            case pf::AlignStatus::noTrack: return "track is silent";
            case pf::AlignStatus::unrelated: return "the two don't share a sound";
        }
        return "?";
    }
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: %s <reference.wav> <track.wav> [max-lag-ms]\n", argv[0]);
        return 2;
    }
    std::vector<float> ref, trk;
    double fsRef = 0.0, fsTrk = 0.0;
    if (! readWav (argv[1], ref, fsRef)) { std::fprintf (stderr, "can't read %s\n", argv[1]); return 1; }
    if (! readWav (argv[2], trk, fsTrk)) { std::fprintf (stderr, "can't read %s\n", argv[2]); return 1; }
    if (fsRef != fsTrk) { std::fprintf (stderr, "sample rates differ (%.0f vs %.0f)\n", fsRef, fsTrk); return 1; }

    pf::AnalyzerSettings s;
    s.sampleRate = fsRef;
    if (argc > 3) s.maxLagMs = std::stod (argv[3]);
    const auto r = pf::analyze (ref.data(), trk.data(), (int) std::min (ref.size(), trk.size()), s);

    std::printf ("status      %s\n", statusText (r.status));
    std::printf ("rotate      %+.1f deg\n", r.correction.phaseDeg);
    std::printf ("polarity    %s\n", r.correction.invert ? "flipped" : "as is");
    std::printf ("low end     %+.2f dB summed\n", r.lowEndGainDb);
    std::printf ("full band   %+.2f dB summed\n", r.sumGainDb);
    if (r.hasOffset)
        std::printf ("offset      track %s by %.3f ms (moving it would add %+.1f dB)\n", r.offsetSamples > 0 ? "late" : "early",
                     std::abs (r.offsetSamples) / s.sampleRate * 1000.0, r.timeFixDb);
    std::printf ("coherence   %.0f %%\n", 100.0 * r.coherence);
    std::printf ("confidence  %.0f %%\n", 100.0 * r.confidence);
    std::printf ("analysed    %.1f s\n", r.activeSeconds);
    return r.status == pf::AlignStatus::ok ? 0 : 1;
}
