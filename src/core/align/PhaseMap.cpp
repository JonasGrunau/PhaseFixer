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

int PhaseMap::indexAt (double ppq) const noexcept
{
    const auto it = std::upper_bound (points.begin(), points.end(), ppq + 1e-9,
                                      [] (double p, const MapPoint& m) { return p < m.ppq; });
    return (int) (it - points.begin()) - 1;
}

double PhaseMap::angleAt (double ppq, double bpm) const noexcept
{
    if (points.empty()) return 0.0;
    const int i = indexAt (ppq);
    if (i < 0) return points.front().deg;
    if (i + 1 >= (int) points.size()) return points.back().deg;

    const auto& a = points[(size_t) i];
    const auto& b = points[(size_t) i + 1];
    const double msPerPpq = 60000.0 / std::max (bpm, 1.0);
    const double gapMs = (b.ppq - a.ppq) * msPerPpq;
    const double glideMs = std::clamp (0.5 * gapMs, kMinGlideMs, kMaxGlideMs);
    const double land = b.ppq - kLandMs / msPerPpq, start = land - glideMs / msPerPpq;
    if (ppq <= start) return a.deg;
    if (ppq >= land) return b.deg;
    const double t = (ppq - start) / (land - start);
    const double f = 0.5 - 0.5 * std::cos (std::numbers::pi * t);
    return wrapDeg (a.deg + wrapDeg (b.deg - a.deg) * f);
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
