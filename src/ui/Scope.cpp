#include "Scope.h"

#include "align/Rotator.h"

#include <limits>

using namespace theme;

namespace
{
    constexpr double kMarginMs = 60.0;   // around the window: the Hilbert filter needs ~43 ms of context

    // A tidy grid step (ms) giving 5-10 divisions across `span` ms.
    float gridStep (float span)
    {
        for (float s : { 0.1f, 0.2f, 0.5f, 1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f, 200.f })
            if (span / s <= 10.f) return s;
        return 500.f;
    }

    // One signal reduced to what a pixel column can show: its range, the
    // sample in the middle, and the last one (to join up with the next column).
    struct Column { float lo, hi, mid, last; };

    const char* const kTriggerNames[] = { "Hit", "Beat", "Bar" };

    juce::String barsName (float bars)
    {
        if (juce::exactlyEqual (bars, 0.25f)) return juce::String (juce::CharPointer_UTF8 ("\xc2\xbc"));   // 1/4
        if (juce::exactlyEqual (bars, 0.5f)) return juce::String (juce::CharPointer_UTF8 ("\xc2\xbd"));    // 1/2
        return juce::String ((int) bars);
    }
} // namespace

Scope::Scope (PhaseFuckerProcessor& p)
    : proc (p), phaseAttachment (p.param (ids::phase), [] (float) {})
{
    zoomMs = juce::jlimit (kMinZoom, kMaxZoom, p.scopeZoomMs);
    if (p.scopeBars > 0.f) lastBars = p.scopeBars;
    setOpaque (true);
}

juce::Rectangle<float> Scope::plotArea() const
{
    // The left gutter holds the level scale, as wide as the phase strip's.
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (18.f).withTrimmedLeft (34.f);
}

juce::Rectangle<float> Scope::holdArea() const
{
    return { (float) getWidth() - 46.f, 2.f, 46.f, 17.f };
}

juce::Rectangle<float> Scope::triggerArea (int i) const
{
    const float widths[] = { 36.f, 42.f, 36.f };
    float x = 150.f;
    for (int k = 0; k < i; ++k) x += widths[k] + 2.f;
    return { x, 2.f, widths[i], 17.f };
}

juce::Rectangle<float> Scope::barsArea (int i) const
{
    return { 316.f + (float) i * 28.f, 2.f, 26.f, 17.f };
}

float Scope::barSpan (float bars) const
{
    return juce::jlimit (kMinZoom, kMaxSpan, (float) (bars * knownBar * 60000.0 / knownBpm));
}

float Scope::span() const
{
    return proc.scopeBars > 0.f ? barSpan (proc.scopeBars) : zoomMs;
}

float Scope::grabSpan() const
{
    // Zoomed in, a grab still holds the bar length the zoom started from:
    // the height is measured over it, so it doesn't change with the zoom.
    return std::max (span(), barSpan (lastBars));
}

float Scope::startOffset (float s) const
{
    // A hit gets some lead-in; a beat or bar starts the window.
    return (proc.scopeTrigger == Trigger::hit || fallback ? 0.2f : 0.04f) * s;
}

void Scope::setZoom (float ms)
{
    zoomMs = juce::jlimit (kMinZoom, kMaxZoom, ms);
    proc.scopeZoomMs = zoomMs;
    proc.scopeBars = 0.f;
    repaint();
}

void Scope::setBars (float bars)
{
    proc.scopeBars = lastBars = bars;
    refresh();
    repaint();
}

bool Scope::grab (const pf::Capture& cap, juce::int64 a, bool findOnset)
{
    fs = cap.sampleRate();
    const float g = grabSpan();
    const int pre = (int) ((startOffset (g) + kMarginMs) * 0.001 * fs);
    const int post = (int) ((g - startOffset (g) + kMarginMs) * 0.001 * fs);
    const int n = pre + post;
    const juce::int64 from = std::max<juce::int64> (0, a - pre);   // zero-padded at the very start
    const int pad = (int) (from - (a - pre));
    std::vector<float> r ((size_t) n, 0.f), t ((size_t) n, 0.f);
    if (! cap.copy (from, n - pad, r.data() + pad, t.data() + pad)) return false;

    int onset = pre;
    if (findOnset)
    {
        // Hits are known to a block; put 0 ms where the reference's
        // transient starts: the first sample reaching 30 % of the peak after it.
        const int s0 = std::max (0, pre - 2 * pf::Capture::kBlock), s1 = std::min (n, pre + (int) (0.012 * fs));
        float peak = 0.f;
        for (int i = s0; i < s1; ++i) peak = std::max (peak, std::abs (r[(size_t) i]));
        for (int i = s0; i < s1; ++i)
            if (std::abs (r[(size_t) i]) >= 0.3f * peak) { onset = i; break; }
        onset = std::max (0, onset - (int) (0.0003 * fs));
    }

    hil = pf::hilbertOffline (t.data(), n, fs);

    // The height, from the whole grab (not the margins): the reference's
    // peak, and the track at the reference's level. The track is matched
    // by its energy averaged over every turn, and bounded by its envelope,
    // which no turn exceeds, so turning the phase moves neither.
    const int m = (int) (kMarginMs * 0.001 * fs);
    double er = 0.0, et = 0.0;
    float peakRef = 0.f, peakEnv = 0.f;
    for (int i = m; i < n - m; ++i)
    {
        const float x = r[(size_t) i], y = t[(size_t) i], h = hil[(size_t) i];
        er += (double) x * x;
        et += 0.5 * ((double) y * y + (double) h * h);
        peakRef = std::max (peakRef, std::abs (x));
        peakEnv = std::max (peakEnv, y * y + h * h);
    }
    match = er > 1e-12 && et > 1e-12 ? (float) juce::jlimit (0.03, 30.0, std::sqrt (er / et)) : 1.f;
    // A grab that is a little louder or quieter keeps the height (the plot
    // leaves 14 % headroom); only clearly different material rescales it.
    const float l = std::max ({ 1e-6f, peakRef, match * std::sqrt (peakEnv) });
    if (level <= 0.f || l > level * 1.15f || l < level * 0.7f)
        level = l;

    grabPpq.resize ((size_t) (n / pf::Capture::kBlock + 2));
    for (size_t b = 0; b < grabPpq.size(); ++b)
        grabPpq[b] = cap.timelineAt (a - pre + (juce::int64) b * pf::Capture::kBlock);
    grabLiveTurn = proc.getLiveTurn();
    ref = std::move (r);
    raw = std::move (t);
    start = a - pre;
    grabbed = a;
    grabbedSpan = g;
    anchor = start + onset;
    dirty = true;
    return true;
}

void Scope::showHit (juce::int64 sample, int holdMs)
{
    if (sample < 0 || (proc.scopeTrigger != Trigger::hit && ! fallback)) return;
    proc.withCapture ([&] (const pf::Capture& cap) {
        if (grab (cap, sample, true))
            holdUntil = juce::Time::getMillisecondCounter() + (juce::uint32) holdMs;
    });
    recompute();
}

void Scope::refresh()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (const auto t = proc.getTransport(); t.valid)
    {
        knownBpm = t.bpm;
        knownBar = t.numerator * 4.0 / t.denominator;
    }
    proc.withCapture ([&] (const pf::Capture& cap) {
        // Zoomed out past what was grabbed: the same anchor, wider.
        if (grabbed >= 0 && grabSpan() > grabbedSpan + 0.5f)
            grab (cap, grabbed, proc.scopeTrigger == Trigger::hit || fallback);

        if (frozen || dragging || now < holdUntil) return;

        if (proc.scopeTrigger != Trigger::hit)
        {
            const auto t = proc.getTransport();
            const double rate = cap.sampleRate();
            fallback = ! t.valid;
            if (t.valid)
            {
                // The latest beat (or bar) whose whole window has been
                // captured. Showing several bars, it steps by that many,
                // counted from bar 1, so bars 1-2, 3-4, ... stand still.
                // Where the playhead was is read from the capture, not
                // worked back from where it is now: a loop that wrapped
                // meanwhile would send that into the wrong pass.
                const double bar = t.numerator * 4.0 / t.denominator, perSample = t.bpm / 60.0 / rate;
                double unit = proc.scopeTrigger == Trigger::bar ? bar : 4.0 / t.denominator;
                if (proc.scopeTrigger == Trigger::bar && proc.scopeBars > 1.f) unit = bar * std::round (proc.scopeBars);
                const float g = grabSpan();
                const auto done = cap.written() - (juce::int64) ((g - startOffset (g) + kMarginMs) * 0.001 * rate);
                if (const double p = cap.timelineAt (done); std::isfinite (p))
                {
                    const double origin = t.barPpq - std::floor (t.barPpq / unit) * unit;
                    const double boundaryPpq = origin + std::floor ((p - origin) / unit) * unit;
                    const auto boundary = done - (juce::int64) std::llround ((p - boundaryPpq) / perSample);
                    // The same stretch of timeline all the way (no jump in
                    // between), and new. Checked a little after the boundary,
                    // where the timeline must have run on from it.
                    const int past = 2 * pf::Capture::kBlock;
                    const double pb = cap.timelineAt (boundary + past);
                    if (std::isfinite (pb) && std::abs (pb - (boundaryPpq + past * perSample)) < 0.02 && std::abs (boundary - grabbed) > 64)
                        grab (cap, boundary, false);
                }
                return;
            }
        }

        // Hits: a newer one replaces the shown one every ~0.7 s, a much stronger one at once.
        float score = 0.f;
        const auto h = cap.strongestHit (1.5, (int) ((grabSpan() + kMarginMs) * 0.001 * cap.sampleRate()), score);
        if (h >= 0 && h != grabbed && (now - lastGrab > 700 || score > heldScore + 6.f || grabbed < 0))
            if (grab (cap, h, true))
            {
                heldScore = score;
                lastGrab = now;
            }
    });
    recompute();
}

double Scope::ppqAt (double index) const
{
    if (grabPpq.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    const double b = index / pf::Capture::kBlock;
    const auto i = (size_t) std::clamp ((int) std::floor (b), 0, (int) grabPpq.size() - 2);
    return grabPpq[i] + (grabPpq[i + 1] - grabPpq[i]) * (b - (double) i);   // NaN stays NaN
}

double Scope::indexOf (double ppq) const
{
    for (size_t i = 0; i + 1 < grabPpq.size(); ++i)
    {
        const double a = grabPpq[i], b = grabPpq[i + 1];
        if (std::isfinite (a) && std::isfinite (b) && b > a && ppq >= a && ppq < b)
            return ((double) i + (ppq - a) / (b - a)) * pf::Capture::kBlock;
    }
    return -1.0;
}

void Scope::recompute()
{
    const auto c = proc.correction();
    const bool bypass = proc.isComparing(), perHit = proc.isPerHit();
    const bool same = juce::exactlyEqual (c.phaseDeg, shown.phaseDeg) && c.invert == shown.invert && bypass == shownBypass
                   && perHit == shownPerHit && (! perHit || proc.getMapRevision() == shownRevision);
    if (same && ! dirty) return;
    shown = c;
    shownBypass = bypass;
    shownPerHit = perHit;
    shownRevision = proc.getMapRevision();
    dirty = false;
    fixed.resize (raw.size());
    if (perHit) map = proc.getMap();
    if (bypass) fixed = raw;
    else if (raw.empty()) {}
    else if (! perHit || map.empty())
        pf::rotateOffline (raw.data(), hil.data(), (int) raw.size(), c, fixed.data());
    else
    {
        // The map's turn along the timeline (where it's known; elsewhere
        // what was playing at the grab), plus the knob's offset; round the
        // loop when the host loops, as the audio thread plays it.
        const auto t = proc.getTransport();
        const double bpm = t.bpm, knob = c.totalDeg();
        const double loopStart = t.looping ? t.loopStart : 0.0, loopEnd = t.looping ? t.loopEnd : 0.0;
        const int n = (int) raw.size();
        constexpr int chunk = 16;
        for (int i = 0; i < n; i += chunk)
        {
            const double ppq = ppqAt (i + 0.5 * chunk);
            pf::Correction turn;
            turn.phaseDeg = std::isfinite (ppq) ? pf::wrapDeg (map.angleAt (ppq, bpm, loopStart, loopEnd) + knob) : grabLiveTurn;
            pf::rotateOffline (raw.data() + i, hil.data() + i, std::min (chunk, n - i), turn, fixed.data() + i);
        }
    }
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    paintOffscreen (g, getLocalBounds(), buffer, [this] (juce::Graphics& o) { draw (o); });
}

void Scope::draw (juce::Graphics& g)
{
    g.fillAll (col::paper);
    const auto plot = plotArea();
    const float span = this->span(), t0 = -startOffset();
    const auto xAt = [&] (float ms) { return plot.getX() + (ms - t0) / span * plot.getWidth(); };

    sectionTag (g, "A", "Scope", { 0.f, 4.f });

    // Trigger switch and HOLD: small tags, the active one inked.
    const auto tag = [&] (juce::Rectangle<float> r, const juce::String& name, bool on) {
        g.setColour (on ? col::ink : col::paper);
        g.fillRoundedRectangle (r.reduced (1.f, 1.f), kRadius);
        g.setColour (col::ink);
        g.drawRoundedRectangle (r.reduced (1.f, 1.f), kRadius, 1.f);
        caption (g, name, r, juce::Justification::centred, on ? col::paper : col::ink, 9.f);
    };
    caption (g, "Trigger", { 96.f, 3.f, 52.f, 15.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
    for (int i = 0; i < 3; ++i)
        tag (triggerArea (i), kTriggerNames[i], (int) proc.scopeTrigger == i);
    caption (g, "Bars", { 282.f, 3.f, 34.f, 15.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
    for (int i = 0; i < 5; ++i)
        tag (barsArea (i), barsName (kBarChoices[i]), juce::exactlyEqual (proc.scopeBars, kBarChoices[i]));
    tag (holdArea(), "Hold", frozen);

    // Legend.
    {
        float x = (float) getWidth() - 60.f;
        const auto item = [&] (const juce::String& name, juce::Colour c, bool dotted) {
            g.setFont (label (9.5f));
            const float w = textWidth (g.getCurrentFont(), name.toUpperCase()) + 4.f;
            x -= w;
            caption (g, name, { x, 3.f, w, 15.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
            x -= 20.f;
            g.setColour (c);
            if (dotted)
                for (float d = 0.f; d < 14.f; d += 3.f) g.fillRect (x + d, 10.f, 1.5f, 1.5f);
            else
                g.fillRect (x, 10.f, 14.f, 1.8f);
            x -= 14.f;
        };
        item ("before", col::accent.withAlpha (0.6f), true);
        item ("track", col::accent, false);
        item ("reference", col::ink, false);
    }

    // Grid: one fill per colour.
    const float step = gridStep (span);
    {
        juce::Path minor, major;
        for (float t = std::ceil (t0 / (step / 5.f)) * (step / 5.f); t <= t0 + span; t += step / 5.f)
            minor.addRectangle (xAt (t), plot.getY(), 1.f, plot.getHeight());
        for (float t = std::ceil (t0 / step) * step; t <= t0 + span + 1e-4f; t += step)
            major.addRectangle (xAt (t), plot.getY(), 1.f, plot.getHeight());
        major.addRectangle (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.f);
        g.setColour (col::grid);
        g.fillPath (minor);
        g.setColour (col::hairline);
        g.fillPath (major);
        g.drawRect (plot, 1.f);
    }

    // Ruler.
    g.setFont (mono (9.5f));
    for (float t = std::ceil (t0 / step) * step; t <= t0 + span + 1e-4f; t += step)
    {
        const float x = xAt (t);
        g.setColour (col::inkFaint);
        g.fillRect (x, plot.getBottom(), 1.f, 4.f);
        g.setColour (std::abs (t) < 1e-4f ? col::ink : col::inkSoft);
        const auto txt = std::abs (t) < 1e-4f ? juce::String ("0 ms") : signedNumber (t, step < 1.f ? 1 : 0);
        // Kept inside the sheet at both ends.
        const float w = textWidth (g.getCurrentFont(), txt) + 2.f;
        const float left = juce::jlimit (0.f, (float) getWidth() - w, x - 0.5f * w);
        g.drawText (txt, juce::Rectangle<float> (left, plot.getBottom() + 4.f, w, 13.f), juce::Justification::centred, false);
    }

    // What the window is anchored on.
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const auto length = [] (float ms) { return ms >= 1000.f ? theme::fixed (ms / 1000.f, ms >= 9950.f ? 0 : 1) + " s" : theme::fixed (ms, ms < 10.f ? 1 : 0) + " ms"; };
    juce::String info = proc.scopeBars > 0.f ? barsName (proc.scopeBars) + (proc.scopeBars > 1.f ? " BARS = " : " BAR = ") + length (span)
                                             : "ZOOM " + length (span);
    if (proc.scopeTrigger != Trigger::hit)
        info += fallback ? dot + "transport stopped, showing hits" : dot + theme::fixed (proc.getTransport().bpm, 1) + " BPM";
    const auto drawInfo = [&] {
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        g.drawText (info, plot.reduced (8.f, 6.f), juce::Justification::topLeft, false);
    };

    if (ref.empty())
    {
        drawInfo();
        const bool timeline = proc.scopeTrigger != Trigger::hit && ! fallback && proc.getTransport().valid;
        const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
        juce::String waiting = "Waiting for the next " + juce::String (proc.scopeTrigger == Trigger::bar ? "bar" : "beat") + " to fill the window" + ellipsis;
        if (proc.scopeTrigger == Trigger::bar && proc.scopeBars > 1.f)
            waiting = "Waiting for " + juce::String ((int) proc.scopeBars) + " whole bars" + ellipsis;
        g.setColour (col::inkSoft);
        g.setFont (text (13.f));
        g.drawText (timeline ? waiting : juce::String (juce::CharPointer_UTF8 ("Play both tracks \xe2\x80\x94 they show up here.")),
                    plot, juce::Justification::centred, false);
        return;
    }

    // Reduce each signal to what a pixel column can show. Zoomed in far
    // enough that a column holds less than ~1.5 samples, draw the samples.
    const int cols = std::max (1, (int) plot.getWidth());
    const double perCol = span * 0.001 * fs / cols;
    const double first = (double) (anchor - start) + t0 * 0.001 * fs;
    const bool perSample = perCol < 1.5;
    const int n = (int) ref.size();

    const auto reduce = [&] (const std::vector<float>& s, std::vector<Column>& out) {
        out.resize ((size_t) cols);
        for (int c = 0; c < cols; ++c)
        {
            const int a = std::clamp ((int) std::floor (first + c * perCol), 0, n - 1);
            const int b = std::clamp ((int) std::floor (first + (c + 1) * perCol), a + 1, n);
            float lo = s[(size_t) a], hi = lo;
            for (int i = a + 1; i < b; ++i)
            {
                lo = std::min (lo, s[(size_t) i]);
                hi = std::max (hi, s[(size_t) i]);
            }
            out[(size_t) c] = { lo, hi, s[(size_t) std::clamp ((int) (first + (c + 0.5) * perCol), 0, n - 1)], s[(size_t) (b - 1)] };
        }
    };
    std::vector<Column> cr, cf, cw;
    reduce (ref, cr);
    reduce (fixed, cf);
    reduce (raw, cw);

    // The track is drawn at the reference's level (by energy: a turn
    // changes a signal's peaks, not its energy, so matching peaks would make
    // a perfect alignment look off). Both share one scale, set per grab.
    const float peak = std::max (level, 1e-6f);
    const float pr = 1.f / peak, pt = match / peak;   // reference and track: value -> fraction of the half height
    const float half = plot.getHeight() * 0.5f * 0.86f;
    const auto yOf = [&] (float v, float scale) { return plot.getCentreY() - v * scale * half; };

    // The scale reads the reference in dBFS; the track is drawn at its level,
    // so the info line says by how much it was raised or lowered for that.
    const float matchDb = 20.f * std::log10 (match);
    if (std::abs (matchDb) >= 0.5f)
        info += dot + "track shown " + signedNumber (matchDb, 1) + " dB";
    drawInfo();

    // Level scale: every 6 dB down from the top edge while the lines stay
    // 20 px clear of the centre, mirrored below it. Silence is the centre.
    {
        juce::Path lines, ticks;
        g.setFont (mono (9.5f));
        const auto mark = [&] (const juce::String& s, float y, juce::Colour c) {
            g.setColour (c);
            g.drawText (s, juce::Rectangle<float> (0.f, juce::jlimit (plot.getY() - 2.f, plot.getBottom() - 10.f, y - 6.f), 30.f, 12.f),
                        juce::Justification::centredRight, false);
        };
        mark ("dB", plot.getY() + 4.f, col::inkFaint);
        mark (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")), plot.getCentreY(), col::inkSoft);   // −∞
        ticks.addRectangle (plot.getX() - 4.f, plot.getCentreY(), 4.f, 1.f);
        const float topDb = 20.f * std::log10 (peak * plot.getHeight() * 0.5f / half);
        for (float db = 6.f * std::floor (topDb / 6.f);; db -= 6.f)
        {
            const float d = std::pow (10.f, db / 20.f) * pr * half;
            if (d < 20.f) break;
            const auto txt = signedNumber (db, 0);
            for (float y : { plot.getCentreY() - d, plot.getCentreY() + d })
            {
                lines.addRectangle (plot.getX(), y, plot.getWidth(), 1.f);
                ticks.addRectangle (plot.getX() - 4.f, y, 4.f, 1.f);
                if (y >= plot.getY() + 16.f)   // clear of the unit
                    mark (txt, y, col::inkSoft);
            }
        }
        g.setColour (col::grid);
        g.fillPath (lines);
        g.setColour (col::inkFaint);
        g.fillPath (ticks);
    }

    const auto xCol = [&] (int c) { return plot.getX() + (float) c + 0.5f; };
    const auto xSample = [&] (int i) { return plot.getX() + (float) ((i - first) / perCol); };

    // Zoomed in: a line through the samples. Zoomed out: per column, the
    // range it holds (joined to the previous column), drawn as one filled
    // outline: top edge forward, bottom edge back. One fill instead of
    // stroking a zigzag of thousands of points or filling a bar per column.
    const auto drawTrace = [&] (const std::vector<float>& s, const std::vector<Column>& col, float scale, float thickness) {
        juce::Path p;
        if (perSample)
        {
            const int i0 = std::max (0, (int) std::floor (first) - 1);
            const int i1 = std::min (n - 1, (int) std::ceil (first + cols * perCol) + 1);
            p.preallocateSpace (3 * (i1 - i0 + 1));
            for (int i = i0; i <= i1; ++i)
            {
                const juce::Point<float> at (xSample (i), yOf (s[(size_t) i], scale));
                if (i == i0) p.startNewSubPath (at); else p.lineTo (at);
            }
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::beveled));
            return;
        }
        std::vector<float> bottom ((size_t) cols);
        p.preallocateSpace (6 * cols + 3);
        for (int c = 0; c < cols; ++c)
        {
            const auto& k = col[(size_t) c];
            const float prev = c > 0 ? col[(size_t) c - 1].last : k.mid;
            float top = yOf (std::max (k.hi, prev), scale), low = yOf (std::min (k.lo, prev), scale);
            if (low - top < thickness)
            {
                const float m = 0.5f * (top + low);
                top = m - 0.5f * thickness;
                low = m + 0.5f * thickness;
            }
            bottom[(size_t) c] = low;
            if (c == 0) p.startNewSubPath (xCol (c), top); else p.lineTo (xCol (c), top);
        }
        for (int c = cols - 1; c >= 0; --c) p.lineTo (xCol (c), bottom[(size_t) c]);
        p.closeSubPath();
        g.fillPath (p);
    };

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.reduced (1.f).toNearestInt());

    // What cancels: the area between reference and corrected track, washed,
    // with a hatch line every fourth column. Two fills.
    {
        juce::Path wash, lines;
        wash.preallocateSpace (6 * cols + 3);
        lines.preallocateSpace (cols / 4 * 16 + 16);
        for (int c = 0; c < cols; ++c)
        {
            const float y = yOf (cr[(size_t) c].mid, pr);
            if (c == 0) wash.startNewSubPath (xCol (c), y); else wash.lineTo (xCol (c), y);
        }
        for (int c = cols - 1; c >= 0; --c)
        {
            const float a = yOf (cr[(size_t) c].mid, pr), b = yOf (cf[(size_t) c].mid, pt);
            wash.lineTo (xCol (c), b);
            if (c % 4 == 0 && std::abs (a - b) > 0.5f)
                lines.addRectangle (xCol (c) - 0.5f, std::min (a, b), 1.f, std::abs (a - b));
        }
        wash.closeSubPath();
        wash.setUsingNonZeroWinding (false);
        g.setColour (col::accentWash);
        g.fillPath (wash);
        g.setColour (col::accent.withAlpha (0.3f));
        g.fillPath (lines);
    }

    // Before: a dot every third column.
    {
        juce::Path dots;
        dots.preallocateSpace (cols / 3 * 16 + 16);
        for (int c = 1; c < cols; c += 3)
            dots.addRectangle (xCol (c) - 0.75f, yOf (cw[(size_t) c].mid, pt) - 0.75f, 1.5f, 1.5f);
        g.setColour (col::accent.withAlpha (0.55f));
        g.fillPath (dots);
    }

    g.setColour (col::ink);
    drawTrace (ref, cr, pr, 1.4f);
    g.setColour (col::accent);
    drawTrace (fixed, cf, pt, 1.8f);

    // Per hit: where the map's hits fall, with their turns.
    if (shownPerHit && ! map.empty())
    {
        g.setFont (mono (9.f));
        float lastLabel = -1e9f;
        for (const auto& p : map.points)
        {
            const double i = indexOf (p.ppq);
            if (i < 0.0) continue;
            const float x = xSample ((int) std::lround (i)) + (float) ((i - std::round (i)) / perCol);
            if (x < plot.getX() || x > plot.getRight()) continue;
            g.setColour (col::accent.withAlpha (0.45f));
            g.fillRect (x, plot.getY(), 1.f, plot.getHeight());
            if (x - lastLabel >= 40.f)
            {
                const auto txt = signedNumber (p.deg, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
                const float w = textWidth (g.getCurrentFont(), txt) + 6.f;
                const auto box = juce::Rectangle<float> (x + 2.f, plot.getBottom() - 15.f, w, 13.f);
                g.setColour (col::paper);
                g.fillRect (box);
                g.setColour (col::accent);
                g.drawText (txt, box, juce::Justification::centred, false);
                lastLabel = x;
            }
        }
    }

    // Anchor marker.
    const float hx = xAt (0.f);
    juce::Path tri;
    tri.addTriangle (hx - 4.f, plot.getY() + 1.f, hx + 4.f, plot.getY() + 1.f, hx, plot.getY() + 6.f);
    g.setColour (col::ink);
    g.fillPath (tri);

    if (isMouseOver() && ! dragging)
    {
        const juce::String dot2 (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        caption (g, (shownPerHit ? "Drag to turn the whole map" : "Drag to turn the phase") + dot2 + "Shift fine" + dot2 + "Wheel to zoom"
                        + dot2 + "Double-click: " + barsName (lastBars) + (lastBars > 1.f ? " bars" : " bar"),
                 plot.reduced (8.f, 6.f), juce::Justification::bottomRight, col::inkSoft, 9.f);
    }
}

void Scope::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (triggerArea (i).contains (e.position))
        {
            proc.scopeTrigger = (Trigger) i;
            grabbed = -1;
            holdUntil = 0;
            fallback = false;
            refresh();
            repaint();
            return;
        }
    for (int i = 0; i < 5; ++i)
        if (barsArea (i).contains (e.position))
        {
            setBars (kBarChoices[i]);
            return;
        }
    if (holdArea().contains (e.position))
    {
        frozen = ! frozen;
        repaint();
        return;
    }
    if (! plotArea().contains (e.position)) return;
    dragging = true;
    lastX = e.position.x;
    auto& p = proc.param (ids::phase);
    dragPhase = p.convertFrom0to1 (p.getValue());
    phaseAttachment.beginGesture();
}

void Scope::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dx = e.position.x - lastX;
    lastX = e.position.x;
    // Half a degree per pixel; wraps around at +-180 like a dial with no stop.
    dragPhase = (float) std::remainder (dragPhase + dx * (e.mods.isShiftDown() ? 0.05f : 0.5f), 360.0);
    phaseAttachment.setValueAsPartOfGesture (dragPhase);
    recompute();
}

void Scope::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    phaseAttachment.endGesture();
    repaint();
}

void Scope::mouseMove (const juce::MouseEvent& e)
{
    // Only the plot is dragged; the header and the ruler get the plain pointer.
    setMouseCursor (plotArea().contains (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void Scope::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (plotArea().contains (e.position))
        setBars (lastBars);
}

void Scope::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    // From whatever is shown (a bar length too) into a free zoom.
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    setZoom (span() * std::pow (1.25f, -d * 4.f * (w.isReversed ? -1.f : 1.f)));
}
