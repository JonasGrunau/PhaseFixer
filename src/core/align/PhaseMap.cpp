#include "PhaseMap.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace pf
{

double wrapDeg (double deg) noexcept
{
    deg = std::remainder (deg, 360.0);
    return deg <= -180.0 ? deg + 360.0 : deg;
}

double timelineBefore (double ppq, double quarters, double loopStart, double loopEnd) noexcept
{
    const double q = ppq - quarters, len = loopEnd - loopStart;
    if (len <= 0.0 || ppq < loopStart || ppq >= loopEnd || q >= loopStart) return q;
    const double into = std::fmod (q - loopStart, len);   // (-len, 0]
    return loopStart + (into < 0.0 ? into + len : into);
}

int PhaseMap::indexAt (double ppq) const noexcept
{
    const auto it = std::upper_bound (points.begin(), points.end(), ppq + 1e-9,
                                      [] (double p, const MapPoint& m) { return p < m.ppq; });
    return (int) (it - points.begin()) - 1;
}

namespace
{
    // From turn a (hit at aPpq) to turn b (hit at bPpq): hold, then the
    // raised-cosine glide in the second half of the gap.
    double glide (double ppq, double aPpq, double aDeg, double bPpq, double bDeg, double bpm) noexcept
    {
        const double msPerPpq = 60000.0 / std::max (bpm, 1.0);
        const double gapMs = (bPpq - aPpq) * msPerPpq;
        const double glideMs = std::clamp (0.5 * gapMs, PhaseMap::kMinGlideMs, PhaseMap::kMaxGlideMs);
        const double land = bPpq - PhaseMap::kLandMs / msPerPpq, start = land - glideMs / msPerPpq;
        if (ppq <= start) return aDeg;
        if (ppq >= land) return bDeg;
        const double t = (ppq - start) / (land - start);
        const double f = 0.5 - 0.5 * std::cos (std::numbers::pi * t);
        return wrapDeg (aDeg + wrapDeg (bDeg - aDeg) * f);
    }
} // namespace

double PhaseMap::angleAt (double ppq, double bpm, double loopStart, double loopEnd) const noexcept
{
    if (points.empty()) return 0.0;
    const int i = indexAt (ppq);

    if (loopEnd > loopStart && ppq >= loopStart && ppq < loopEnd)
    {
        const auto before = [] (const MapPoint& m, double p) { return m.ppq < p; };
        const int first = (int) (std::lower_bound (points.begin(), points.end(), loopStart, before) - points.begin());
        const int end = (int) (std::lower_bound (points.begin(), points.end(), loopEnd, before) - points.begin());
        if (first < end)   // hits inside the loop: the one before comes from the last pass, the next from the next pass
        {
            const double len = loopEnd - loopStart;
            const auto& a = points[(size_t) (i >= first ? i : end - 1)];
            const auto& b = points[(size_t) (i >= first && i + 1 < end ? i + 1 : first)];
            const double aPpq = i >= first ? a.ppq : a.ppq - len;
            const double bPpq = i >= first && i + 1 < end ? b.ppq : b.ppq + (i >= first ? len : 0.0);
            return glide (ppq, aPpq, a.deg, bPpq, b.deg, bpm);
        }
    }

    if (i < 0) return points.front().deg;
    if (i + 1 >= (int) points.size()) return points.back().deg;
    const auto& a = points[(size_t) i];
    const auto& b = points[(size_t) i + 1];
    return glide (ppq, a.ppq, a.deg, b.ppq, b.deg, bpm);
}

void PhaseMap::learn (std::vector<Hit> hits, double fromPpq, double toPpq, double bpm)
{
    const double tol = kSameHitMs * std::max (bpm, 1.0) / 60000.0;
    std::sort (hits.begin(), hits.end(), [] (const Hit& x, const Hit& y) { return x.ppq < y.ppq; });

    // Passes of a loop put the same hit at the same place: average them as
    // vectors, weighted by how sure each one was.
    std::vector<MapPoint> fresh;
    for (size_t i = 0; i < hits.size();)
    {
        size_t j = i;
        std::complex<double> v;
        double ppq = 0.0, gain = 0.0, fit = 0.0, w = 0.0;
        for (; j < hits.size() && hits[j].ppq - hits[i].ppq < tol; ++j)
        {
            const double hw = std::max ((double) hits[j].fit, 0.05);
            v += hw * std::polar (1.0, hits[j].deg * std::numbers::pi / 180.0);
            ppq += hits[j].ppq * hw;
            gain += hits[j].gainDb * hw;
            fit += hits[j].fit * hw;
            w += hw;
        }
        MapPoint p;
        p.ppq = ppq / w;
        p.deg = p.learnedDeg = std::abs (v) > 0.0 ? wrapDeg (std::arg (v) * 180.0 / std::numbers::pi) : 0.0;
        p.gainDb = (float) (gain / w);
        p.fit = (float) (fit / w);
        fresh.push_back (p);
        i = j;
    }

    // Hand edits in the stretch survive; everything else learned there goes.
    std::vector<MapPoint> kept;
    for (const auto& p : points)
        if (p.edited || p.ppq < fromPpq - tol || p.ppq > toPpq + tol)
            kept.push_back (p);

    for (auto& p : fresh)
    {
        const auto near = std::find_if (kept.begin(), kept.end(),
                                        [&] (const MapPoint& k) { return std::abs (k.ppq - p.ppq) < tol; });
        if (near == kept.end())
            kept.push_back (p);
        else if (near->edited)
        {
            near->learnedDeg = p.learnedDeg;
            near->gainDb = p.gainDb;
            near->fit = p.fit;
        }
        else
            *near = p;
    }
    std::sort (kept.begin(), kept.end(), [] (const MapPoint& x, const MapPoint& y) { return x.ppq < y.ppq; });
    points = std::move (kept);
}

void PhaseMap::edit (int index, double deg)
{
    if (index < 0 || index >= (int) points.size()) return;
    auto& p = points[(size_t) index];
    p.deg = wrapDeg (deg);
    p.edited = std::abs (wrapDeg (p.deg - p.learnedDeg)) > 0.05;
    if (! p.edited) p.deg = p.learnedDeg;
}

} // namespace pf
