#include "Analyzer.h"

#include "CrossSpectrum.h"
#include "dsp/Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace pf
{
namespace
{
    using dsp::cplx;

    constexpr double kPi = std::numbers::pi;
    constexpr double kActive = 1e-6;       // -60 dBFS mean square
    constexpr int kBlock = 512;            // level envelope resolution
    constexpr int kHistory = 8;            // blocks a rise is measured against
    constexpr double kLoHz = 25.0, kHiHz = 16000.0;

    int pow2AtLeast (int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    int log2Of (int n)
    {
        int o = 0;
        while ((1 << o) < n) ++o;
        return o;
    }

    // Where comb cancellation is heard: full weight from ~60 Hz to ~2 kHz.
    double hearingWeight (double hz)
    {
        const double lo = 1.0 / (1.0 + std::pow (45.0 / std::max (hz, 1.0), 4.0));
        const double hi = 0.3 + 0.7 / (1.0 + std::pow (hz / 2500.0, 2.0));
        return lo * hi;
    }

    // What the rotation aims for: correlation near 1 below ~1 kHz (PHA-979's
    // rule of thumb), the highs only nudging. Per octave, not per bin (there
    // are ten times more bins in 300 Hz-3 kHz than in 30-300 Hz), so the
    // 1 / f.
    double focusWeight (double hz)
    {
        hz = std::max (hz, 1.0);
        const double lo = 1.0 / (1.0 + std::pow (30.0 / hz, 4.0));
        const double hi = 0.08 + 0.92 / (1.0 + std::pow (hz / 1000.0, 4.0));
        return lo * hi * 100.0 / hz;
    }

    // Analysis band with raised-cosine edges a third of an octave wide.
    double bandLimit (double hz)
    {
        const auto edge = [] (double x) { return x <= 0.0 ? 0.0 : x >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos (kPi * x); };
        if (hz <= 0.0) return 0.0;
        return edge (3.0 * std::log2 (hz / kLoHz)) * edge (3.0 * std::log2 (kHiHz / hz));
    }

    // Level per block and how far it rose (dB) over the blocks before it.
    struct Envelope
    {
        std::vector<float> ms, rise;

        Envelope (const float* x, int n)
        {
            const int nb = n / kBlock;
            ms.resize ((size_t) nb);
            rise.assign ((size_t) nb, 0.f);
            for (int b = 0; b < nb; ++b)
            {
                double acc = 0.0;
                for (int i = 0; i < kBlock; ++i)
                    acc += (double) x[b * kBlock + i] * x[b * kBlock + i];
                ms[(size_t) b] = (float) (acc / kBlock);
            }
            for (int b = kHistory; b < nb; ++b)
            {
                double prev = 0.0;
                for (int k = 1; k <= kHistory; ++k) prev += ms[(size_t) (b - k)];
                rise[(size_t) b] = (float) (10.0 * std::log10 ((ms[(size_t) b] + 1e-10) / (prev / kHistory + 1e-10)));
            }
        }

        float peak() const { return ms.empty() ? 0.f : *std::max_element (ms.begin(), ms.end()); }
        float riseAt (int b) const { return b >= 0 && b < (int) rise.size() ? rise[(size_t) b] : 0.f; }
    };

    struct Input
    {
        const float* ref;
        const float* trk;
        int n;
        double fs;
        int fftSize, hop;
        Envelope envRef, envTrk;
        std::vector<float> window;
        dsp::Fft fft;
        int k0, k1;                    // analysis band in bins
        std::vector<double> limit;     // band-limit weight per bin
        std::vector<double> lagBias;   // window overlap at each lag, 1 at lag 0
    };

    struct Frames
    {
        int used = 0;
        double seconds = 0.0;
        std::vector<std::vector<cplx>> segments;   // S_ab per group of frames
    };

    double meanSquare (const float* x, int n)
    {
        double acc = 0.0;
        for (int i = 0; i < n; ++i) acc += (double) x[i] * x[i];
        return acc / n;
    }

    // Accumulates the frames where both play into `spec`. Track frames start
    // `offset` samples after their reference frames.
    Frames accumulate (Input& in, int offset, CrossSpectrum& spec, int framesPerSegment)
    {
        const int N = in.fftSize;
        spec.reset (N, in.fs);

        struct F { int start; double ea, eb; float onset; };
        std::vector<F> list;
        double peakA = 0.0, peakB = 0.0;
        for (int s = std::max (0, -offset); s + N <= in.n && s + offset + N <= in.n; s += in.hop)
        {
            F f { s, meanSquare (in.ref + s, N), meanSquare (in.trk + s + offset, N), 0.f };
            // Sharpest hit both share inside the frame (middle half, where the window is open).
            const int b0 = (s + N / 4) / kBlock, b1 = (s + 3 * N / 4) / kBlock;
            const int shiftBlocks = (int) std::lround ((double) offset / kBlock);
            for (int b = b0; b < b1; ++b)
                f.onset = std::max (f.onset, std::min (in.envRef.riseAt (b), in.envTrk.riseAt (b + shiftBlocks)));
            peakA = std::max (peakA, f.ea);
            peakB = std::max (peakB, f.eb);
            list.push_back (f);
        }

        Frames out;
        std::vector<cplx> a ((size_t) spec.numBins()), b ((size_t) spec.numBins()), scratch;
        for (const auto& f : list)
        {
            const bool active = f.ea > kActive && f.eb > kActive && f.ea > peakA * 1e-4 && f.eb > peakB * 1e-4;
            if (! active) continue;

            // Hits count up to 3x, and each frame weighs by its RMS rather
            // than its energy, so a loud chorus doesn't drown the verse.
            const double w = (1.0 + std::clamp (f.onset / 6.0, 0.0, 2.0)) / std::sqrt (std::sqrt (f.ea * f.eb));
            in.fft.forwardRealPair (in.ref + f.start, in.trk + f.start + offset, N, in.window.data(), a.data(), b.data(), scratch);
            spec.add (a.data(), b.data(), w);

            if (framesPerSegment > 0)
            {
                const auto seg = (size_t) (out.used / framesPerSegment);
                if (seg >= out.segments.size())
                    out.segments.emplace_back ((size_t) spec.numBins(), cplx {});
                auto& s = out.segments[seg];
                for (int k = 0; k < spec.numBins(); ++k)
                    s[(size_t) k] += w * std::conj (a[(size_t) k]) * b[(size_t) k];
            }
            ++out.used;
        }
        out.seconds = out.used == 0 ? 0.0 : ((out.used - 1) * in.hop + N / 2) / in.fs;
        return out;
    }

    // Magnitude-squared coherence per bin, corrected for the bias a finite
    // number of frames gives it, lightly smoothed across frequency.
    std::vector<double> coherence (const CrossSpectrum& s)
    {
        const double m = s.effectiveFrames();
        const int nb = s.numBins();
        std::vector<double> raw ((size_t) nb), c ((size_t) nb);
        for (int k = 0; k < nb; ++k)
        {
            double g = std::norm (s.sab[(size_t) k]) / (s.saa[(size_t) k] * s.sbb[(size_t) k] + 1e-30);
            if (m > 1.5) g = (m * g - 1.0) / (m - 1.0);
            raw[(size_t) k] = std::clamp (g, 0.0, 0.98);
        }
        for (int k = 0; k < nb; ++k)
        {
            double acc = 0.0;
            int cnt = 0;
            for (int j = std::max (0, k - 2); j <= std::min (nb - 1, k + 2); ++j, ++cnt)
                acc += raw[(size_t) j];
            c[(size_t) k] = acc / cnt;
        }
        return c;
    }

    // Generalised cross-correlation with the maximum-likelihood weighting.
    // r[i] is lag i (i >= N/2 means i - N); positive lag = track late.
    std::vector<double> gcc (const Input& in, const std::vector<cplx>& sab, const std::vector<double>& coh)
    {
        const int N = in.fftSize;
        std::vector<cplx> z ((size_t) N);
        for (int k = in.k0; k <= in.k1; ++k)
        {
            const double c = coh[(size_t) k];
            const double w = in.limit[(size_t) k] * c / ((1.0 - c) * std::abs (sab[(size_t) k]) + 1e-30);
            z[(size_t) k] = w * sab[(size_t) k];
            z[(size_t) (N - k)] = std::conj (z[(size_t) k]);
        }
        in.fft.inverse (z.data());
        std::vector<double> r ((size_t) N);
        for (int i = 0; i < N; ++i) r[(size_t) i] = z[(size_t) i].real();
        return r;
    }

    double at (const std::vector<double>& r, int lag)
    {
        const int N = (int) r.size();
        return r[(size_t) ((lag % N + N) % N)];
    }

    // Re sum_k v[k] e^{j w_k tau}: how well the two add up with the track
    // moved earlier by tau, weighted by whatever v carries.
    double sweep (const std::vector<cplx>& v, int k0, int k1, double tau, int N)
    {
        const cplx step = std::polar (1.0, 2.0 * kPi * tau / N);
        cplx z = std::polar (1.0, 2.0 * kPi * tau * k0 / N);
        double acc = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            acc += (v[(size_t) k] * z).real();
            z *= step;
        }
        return acc;
    }

    double parabola (double ym, double y0, double yp)
    {
        const double d = ym - 2.0 * y0 + yp;
        return std::abs (d) < 1e-300 ? 0.0 : std::clamp (0.5 * (ym - yp) / d, -0.5, 0.5);
    }

    // The tau within center +- halfRange where |sweep| peaks: grid, then a parabola.
    double refine (const std::vector<cplx>& v, int k0, int k1, int N, double center, double halfRange, double step)
    {
        const int steps = (int) std::ceil (halfRange / step);
        std::vector<double> y ((size_t) (2 * steps + 1));
        int best = steps;
        for (int i = 0; i <= 2 * steps; ++i)
        {
            y[(size_t) i] = std::abs (sweep (v, k0, k1, center + (i - steps) * step, N));
            if (y[(size_t) i] > y[(size_t) best]) best = i;
        }
        double tau = center + (best - steps) * step;
        if (best > 0 && best < 2 * steps)
            tau += step * parabola (y[(size_t) best - 1], y[(size_t) best], y[(size_t) best + 1]);
        return tau;
    }

    // Linear autocorrelation of the window, normalised: how much two frames
    // still overlap when the signal inside is shifted by `lag`.
    std::vector<double> windowOverlap (const std::vector<float>& w, int maxLag)
    {
        std::vector<double> o ((size_t) maxLag + 1);
        double zero = 0.0;
        for (float x : w) zero += (double) x * x;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double acc = 0.0;
            for (size_t i = 0; i + (size_t) lag < w.size(); i += 4)   // every 4th sample is plenty
                acc += (double) w[i] * w[i + (size_t) lag];
            o[(size_t) lag] = std::max (0.05, 4.0 * acc / zero);
        }
        return o;
    }

    double overlapAt (const Input& in, double lag)
    {
        const auto i = (size_t) std::min ((double) in.lagBias.size() - 1, std::abs (lag));
        return in.lagBias[i];
    }

    struct Candidate
    {
        double tau = 0.0, score = 0.0;
        int sign = 1;
    };

    // Strongest local maxima of |r| (corrected for window overlap) in the
    // search range, at least three samples apart.
    std::vector<int> peakLags (const Input& in, const std::vector<double>& r, int maxLag, int maxCount)
    {
        std::vector<std::pair<double, int>> peaks;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            const double v = std::abs (at (r, lag)) / overlapAt (in, lag);
            const double vm = std::abs (at (r, lag - 1)) / overlapAt (in, lag - 1);
            const double vp = std::abs (at (r, lag + 1)) / overlapAt (in, lag + 1);
            if (v > 0.0 && v >= vm && v >= vp)
                peaks.emplace_back (v, lag);
        }
        std::sort (peaks.begin(), peaks.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::vector<int> out;
        for (const auto& [v, lag] : peaks)
        {
            if ((int) out.size() >= maxCount || v < 0.25 * peaks.front().first) break;
            if (std::none_of (out.begin(), out.end(), [lag = lag] (int o) { return std::abs (o - lag) < 3; }))
                out.push_back (lag);
        }
        return out;
    }

    // Weighted spectra for the sweeps: `energy` scores what you hear, `sharp`
    // (half-whitened) pins the fine position down.
    void sweepVectors (const Input& in, const CrossSpectrum& s, const std::vector<double>& coh,
                       std::vector<cplx>& energy, std::vector<cplx>& sharp, double& norm)
    {
        energy.assign ((size_t) s.numBins(), {});
        sharp.assign ((size_t) s.numBins(), {});
        norm = 0.0;
        for (int k = in.k0; k <= in.k1; ++k)
        {
            const double w = hearingWeight (s.binHz (k)) * in.limit[(size_t) k] * coh[(size_t) k];
            const cplx v = s.sab[(size_t) k];
            energy[(size_t) k] = w * v;
            sharp[(size_t) k] = w * v / std::sqrt (std::abs (v) + 1e-30);
            norm += w * std::sqrt (s.saa[(size_t) k] * s.sbb[(size_t) k]);
        }
    }

} // namespace

AlignResult analyze (const float* ref, const float* trk, int n, const AnalyzerSettings& st)
{
    AlignResult res;
    const double fs = st.sampleRate;
    const int maxLag = std::max (1, (int) std::ceil (st.maxLagMs * 0.001 * fs));
    const int N = pow2AtLeast (std::max (4 * maxLag, 4096));

    Input in { ref, trk, n, fs, N, N / 4, Envelope (ref, n), Envelope (trk, n), dsp::hann (N), dsp::Fft (log2Of (N)), 0, 0, {}, {} };

    if (in.envRef.peak() < kActive) { res.status = AlignStatus::noReference; return res; }
    if (in.envTrk.peak() < kActive) { res.status = AlignStatus::noTrack; return res; }

    // The hit for the scope: the sharpest shared rise, louder hits winning ties.
    {
        float bestScore = -1e9f;
        for (int b = 0; b < (int) in.envRef.ms.size(); ++b)
        {
            const float er = in.envRef.ms[(size_t) b], et = in.envTrk.ms[(size_t) b];
            if (er < kActive || et < kActive) continue;
            const float rise = std::min (in.envRef.riseAt (b), in.envTrk.riseAt (b));
            const float s = rise + std::log10 (std::min (er, et));
            if (s > bestScore)
            {
                bestScore = s;
                res.hitSample = (int64_t) b * kBlock;
            }
        }
    }

    if (n < N) { res.status = AlignStatus::notEnoughSignal; return res; }

    in.k0 = std::max (1, (int) std::floor (kLoHz * N / fs));
    in.k1 = std::min (N / 2 - 1, (int) std::ceil (std::min (kHiHz, 0.45 * fs) * N / fs));
    in.limit.assign ((size_t) N / 2 + 1, 0.0);
    for (int k = in.k0; k <= in.k1; ++k)
        in.limit[(size_t) k] = bandLimit (k * fs / N);
    in.lagBias = windowOverlap (in.window, N / 4 + 2);

    CrossSpectrum spec;
    const int perSegment = std::max (3, (int) std::lround (fs / in.hop));
    const auto frames = accumulate (in, 0, spec, perSegment);
    res.activeSeconds = frames.seconds;
    if (frames.used == 0 || frames.seconds < st.minSeconds) { res.status = AlignStatus::notEnoughSignal; return res; }

    const auto coh = coherence (spec);

    // ---- Rotation ---------------------------------------------------------------
    // Summed energy with the track turned by phi is
    //     sum (S_aa + S_bb) + 2 Re (e^{j phi} sum W S_ab),
    // largest at phi = -arg (sum W S_ab).
    std::vector<double> weight ((size_t) spec.numBins(), 0.0);
    cplx z {};
    double norm = 0.0, cohMean = 0.0, cohWeight = 0.0;
    for (int k = in.k0; k <= in.k1; ++k)
    {
        const double hz = spec.binHz (k);
        const double w = focusWeight (hz) * in.limit[(size_t) k] * coh[(size_t) k];
        const double mag = std::sqrt (spec.saa[(size_t) k] * spec.sbb[(size_t) k]);
        weight[(size_t) k] = w;
        z += w * spec.sab[(size_t) k];
        norm += w * mag;
        const double cw = hearingWeight (hz) * in.limit[(size_t) k] * mag;
        cohMean += cw * coh[(size_t) k];
        cohWeight += cw;
    }
    cohMean = cohWeight > 0.0 ? cohMean / cohWeight : 0.0;
    res.coherence = (float) cohMean;

    const double turnDeg = std::abs (z) > 0.0 ? -std::arg (z) * 180.0 / kPi : 0.0;
    res.correction = Correction::fromTotal (turnDeg);
    // ~1 when one angle lines the shared part up everywhere that counts.
    const double fit = norm > 0.0 ? std::abs (z) / norm : 0.0;

    // Do ~1 s stretches land on the same angle on their own?
    double agreement = 0.7;
    if (frames.segments.size() >= 2)
    {
        cplx unit {};
        double total = 0.0;
        for (const auto& seg : frames.segments)
        {
            cplx zs {};
            for (int k = in.k0; k <= in.k1; ++k)
                zs += weight[(size_t) k] * seg[(size_t) k];
            if (std::abs (zs) <= 0.0) continue;
            unit += zs / std::abs (zs) * std::sqrt (std::abs (zs));
            total += std::sqrt (std::abs (zs));
        }
        if (total > 0.0) agreement = std::abs (unit) / total;
    }

    res.lowEndGainDb = (float) spec.sumGainDb (res.correction, 20.0, 300.0);
    res.sumGainDb = (float) spec.sumGainDb (res.correction, 20.0, 20000.0);

    // ---- Time anchor ------------------------------------------------------------
    const auto r = gcc (in, spec.sab, coh);
    const auto lags = peakLags (in, r, maxLag, 5);
    double psrDb = 0.0;
    if (! lags.empty())
    {
        const double peak = std::abs (at (r, lags.front())) / overlapAt (in, lags.front());
        double acc = 0.0;
        int cnt = 0;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
            if (std::abs (lag - lags.front()) > 8)
            {
                const double v = at (r, lag) / overlapAt (in, lag);
                acc += v * v;
                ++cnt;
            }
        psrDb = cnt > 0 && acc > 0.0 ? 20.0 * std::log10 (peak / std::sqrt (acc / cnt)) : 60.0;

        std::vector<cplx> energy, sharp;
        double unused = 0.0;
        sweepVectors (in, spec, coh, energy, sharp, unused);
        double best = -1.0, tau = 0.0;
        for (int lag : lags)
        {
            const double t = refine (sharp, in.k0, in.k1, N, lag, 1.5, 0.05);
            const double score = std::abs (sweep (energy, in.k0, in.k1, t, N)) / overlapAt (in, t);
            if (score > best) { best = score; tau = t; }
        }

        // Below ~0.1 ms the "offset" is the rotation itself bending the peak.
        if (psrDb >= 14.0 && std::abs (tau) >= std::max (1.0, 1e-4 * fs) && std::abs (tau) <= maxLag - 1.0)
        {
            res.hasOffset = true;
            res.offsetSamples = tau;
            // The best rotation once the track were moved, and what that adds.
            cplx zt {};
            for (int k = in.k0; k <= in.k1; ++k)
                zt += weight[(size_t) k] * spec.sab[(size_t) k] * std::polar (1.0, spec.omega (k) * tau);
            const auto moved = Correction::fromTotal (-std::arg (zt) * 180.0 / kPi);
            const double eMoved = spec.sumEnergy (moved, 20.0, 20000.0, -tau);
            const double eNow = spec.sumEnergy (res.correction, 20.0, 20000.0);
            if (eMoved > 0.0 && eNow > 0.0)
                res.timeFixDb = (float) std::max (0.0, 10.0 * std::log10 (eMoved / eNow));
        }
    }

    // ---- Confidence -------------------------------------------------------------
    const double cFit = std::clamp ((fit - 0.2) / 0.6, 0.0, 1.0);
    const double cCoh = std::clamp ((cohMean - 0.05) / 0.4, 0.0, 1.0);
    const double cAmount = std::clamp (0.4 + 0.3 * frames.seconds, 0.4, 1.0);
    res.confidence = (float) ((0.45 * cFit + 0.3 * cCoh + 0.25 * agreement) * cAmount);

    // Unrelated: little in common, and no sign of a shared arrival either.
    res.status = (cohMean < 0.05 || (fit < 0.15 && psrDb < 14.0)) ? AlignStatus::unrelated : AlignStatus::ok;
    return res;
}

} // namespace pf
