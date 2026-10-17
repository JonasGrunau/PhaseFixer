#include "Scope.h"

#include "align/Rotator.h"

using namespace theme;

namespace
{
    constexpr double kPre = 0.20, kPost = 0.15;   // seconds kept around a hit
    constexpr float kMinZoom = 2.f, kMaxZoom = 100.f;

    // A tidy grid step (ms) giving 5-10 divisions across `span` ms.
    float gridStep (float span)
    {
        for (float s : { 0.1f, 0.2f, 0.5f, 1.f, 2.f, 5.f, 10.f, 20.f })
            if (span / s <= 10.f) return s;
        return 50.f;
    }

    // One signal reduced to what a pixel column can show: its range, the
    // sample in the middle, and the last one (to join up with the next column).
    struct Column { float lo, hi, mid, last; };
} // namespace

Scope::Scope (PhaseFuckerProcessor& p)
    : proc (p), phaseAttachment (p.param (ids::phase), [] (float) {})
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setOpaque (true);
}

juce::Rectangle<float> Scope::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (18.f);
}

juce::Rectangle<float> Scope::holdArea() const
{
    return { (float) getWidth() - 46.f, 2.f, 46.f, 17.f };
}

void Scope::setZoom (float ms)
{
    zoomMs = juce::jlimit (kMinZoom, kMaxZoom, ms);
    repaint();
}

bool Scope::grab (const pf::Capture& cap, juce::int64 h)
{
    fs = cap.sampleRate();
    const int pre = (int) (kPre * fs), post = (int) (kPost * fs), n = pre + post;
    const juce::int64 from = std::max<juce::int64> (0, h - pre);   // zero-padded at the very start
    const int pad = (int) (from - (h - pre));
    std::vector<float> r ((size_t) n, 0.f), t ((size_t) n, 0.f);
    if (! cap.copy (from, n - pad, r.data() + pad, t.data() + pad)) return false;

    // Hits are known to a block; put 0 ms where the reference's transient
    // starts: the first sample reaching 30 % of the peak that follows.
    const int s0 = std::max (0, pre - 2 * pf::Capture::kBlock), s1 = std::min (n, pre + (int) (0.012 * fs));
    float peak = 0.f;
    for (int i = s0; i < s1; ++i) peak = std::max (peak, std::abs (r[(size_t) i]));
    int onset = pre;
    for (int i = s0; i < s1; ++i)
        if (std::abs (r[(size_t) i]) >= 0.3f * peak) { onset = i; break; }
    onset = std::max (0, onset - (int) (0.0003 * fs));

    hil = pf::hilbertOffline (t.data(), n, fs);
    ref = std::move (r);
    raw = std::move (t);
    start = h - pre;
    grabbed = h;
    hit = start + onset;
    dirty = true;
    return true;
}

void Scope::showHit (juce::int64 sample, int holdMs)
{
    if (sample < 0) return;
    proc.withCapture ([&] (const pf::Capture& cap) {
        if (grab (cap, sample))
            holdUntil = juce::Time::getMillisecondCounter() + (juce::uint32) holdMs;
    });
    recompute();
}

void Scope::refresh()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (! frozen && ! dragging && now >= holdUntil)
        proc.withCapture ([&] (const pf::Capture& cap) {
            float score = 0.f;
            const auto h = cap.strongestHit (1.5, (int) (kPost * cap.sampleRate()), score);
            // A newer hit replaces the shown one every ~0.7 s, a much
            // stronger one at once.
            if (h >= 0 && h != grabbed && (now - lastGrab > 700 || score > heldScore + 6.f || hit < 0))
                if (grab (cap, h))
                {
                    heldScore = score;
                    lastGrab = now;
                }
        });
    recompute();
}

void Scope::recompute()
{
    const auto c = proc.correction();
    const bool bypass = proc.isComparing();
    const bool same = juce::exactlyEqual (c.phaseDeg, shown.phaseDeg) && c.invert == shown.invert && bypass == shownBypass;
    if (same && ! dirty) return;
    shown = c;
    shownBypass = bypass;
    dirty = false;
    fixed.resize (raw.size());
    if (bypass) fixed = raw;
    else if (! raw.empty()) pf::rotateOffline (raw.data(), hil.data(), (int) raw.size(), c, fixed.data());
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
    const float span = zoomMs, t0 = -0.2f * span;
    const auto xAt = [&] (float ms) { return plot.getX() + (ms - t0) / span * plot.getWidth(); };

    sectionTag (g, "A", "Scope", { 0.f, 4.f });

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
        const auto h = holdArea();
        g.setColour (frozen ? col::ink : col::paper);
        g.fillRoundedRectangle (h.reduced (2.f, 1.f), kRadius);
        g.setColour (col::ink);
        g.drawRoundedRectangle (h.reduced (2.f, 1.f), kRadius, 1.f);
        caption (g, "Hold", h, juce::Justification::centred, frozen ? col::paper : col::ink, 9.f);
    }

    // Grid: one fill per colour.
    const float step = gridStep (span);
    {
        juce::Path minor, major;
        for (float t = std::ceil (t0 / (step / 5.f)) * (step / 5.f); t <= t0 + span; t += step / 5.f)
            minor.addRectangle (xAt (t), plot.getY(), 1.f, plot.getHeight());
        for (float frac : { 0.25f, 0.75f })
            minor.addRectangle (plot.getX(), plot.getY() + plot.getHeight() * frac, plot.getWidth(), 1.f);
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
        g.drawText (txt, juce::Rectangle<float> (x - 30.f, plot.getBottom() + 4.f, 60.f, 13.f), juce::Justification::centred, false);
    }

    if (ref.empty())
    {
        g.setColour (col::inkSoft);
        g.setFont (text (13.f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Play both tracks \xe2\x80\x94 the last hit they share shows up here.")),
                    plot, juce::Justification::centred, false);
        return;
    }

    // Reduce each signal to what a pixel column can show. Zoomed in far
    // enough that a column holds less than ~1.5 samples, draw the samples.
    const int cols = std::max (1, (int) plot.getWidth());
    const double perCol = span * 0.001 * fs / cols;
    const double first = (double) (hit - start) + t0 * 0.001 * fs;
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

    float pr = 1e-6f, pt = 1e-6f;
    for (int c = 0; c < cols; ++c)
    {
        pr = std::max ({ pr, std::abs (cr[(size_t) c].lo), std::abs (cr[(size_t) c].hi) });
        pt = std::max ({ pt, std::abs (cf[(size_t) c].lo), std::abs (cf[(size_t) c].hi), std::abs (cw[(size_t) c].lo), std::abs (cw[(size_t) c].hi) });
    }
    const float half = plot.getHeight() * 0.5f * 0.86f;
    const auto yOf = [&] (float v, float peak) { return plot.getCentreY() - v / peak * half; };
    const auto xCol = [&] (int c) { return plot.getX() + (float) c + 0.5f; };
    const auto xSample = [&] (int i) { return plot.getX() + (float) ((i - first) / perCol); };

    // Zoomed in: a line through the samples. Zoomed out: per column, the
    // range it holds (joined to the previous column), drawn as one filled
    // outline: top edge forward, bottom edge back. One fill instead of
    // stroking a zigzag of thousands of points or filling a bar per column.
    const auto drawTrace = [&] (const std::vector<float>& s, const std::vector<Column>& col, float peak, float thickness) {
        juce::Path p;
        if (perSample)
        {
            const int i0 = std::max (0, (int) std::floor (first) - 1);
            const int i1 = std::min (n - 1, (int) std::ceil (first + cols * perCol) + 1);
            p.preallocateSpace (3 * (i1 - i0 + 1));
            for (int i = i0; i <= i1; ++i)
            {
                const juce::Point<float> at (xSample (i), yOf (s[(size_t) i], peak));
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
            float top = yOf (std::max (k.hi, prev), peak), low = yOf (std::min (k.lo, prev), peak);
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

    // Hit marker.
    const float hx = xAt (0.f);
    juce::Path tri;
    tri.addTriangle (hx - 4.f, plot.getY() + 1.f, hx + 4.f, plot.getY() + 1.f, hx, plot.getY() + 6.f);
    g.setColour (col::ink);
    g.fillPath (tri);

    // Readout and hints.
    g.setFont (mono (10.f));
    g.setColour (col::inkSoft);
    g.drawText ("ZOOM " + theme::fixed (span, span < 10.f ? 1 : 0) + " ms", plot.reduced (8.f, 6.f), juce::Justification::topLeft, false);
    if (isMouseOver() && ! dragging)
        caption (g, juce::String (juce::CharPointer_UTF8 ("Drag to turn the phase \xc2\xb7 Shift fine \xc2\xb7 Wheel to zoom")),
                 plot.reduced (8.f, 6.f), juce::Justification::bottomRight, col::inkSoft, 9.f);
}

void Scope::mouseDown (const juce::MouseEvent& e)
{
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

void Scope::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (plotArea().contains (e.position) && ! holdArea().contains (e.position))
        setZoom (12.f);
}

void Scope::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    setZoom (zoomMs * std::pow (1.25f, -d * 4.f * (w.isReversed ? -1.f : 1.f)));
}
