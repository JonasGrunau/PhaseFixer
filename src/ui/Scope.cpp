#include "Scope.h"

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
} // namespace

Scope::Scope (PhaseFuckerProcessor& p)
    : proc (p), shiftAttachment (p.param (ids::shift), [] (float) {})
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

juce::Rectangle<float> Scope::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (18.f);
}

juce::Rectangle<float> Scope::holdArea() const
{
    return { (float) getWidth() - 46.f, 2.f, 46.f, 17.f };
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

    ref = std::move (r);
    raw = std::move (t);
    start = h - pre;
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
    if (! frozen && now >= holdUntil)
        proc.withCapture ([&] (const pf::Capture& cap) {
            float score = 0.f;
            const auto h = cap.strongestHit (1.5, (int) (kPost * cap.sampleRate()), score);
            // A newer hit replaces the shown one every ~0.7 s, a much
            // stronger one at once.
            if (h >= 0 && h != hit && (now - lastGrab > 700 || score > heldScore + 6.f || hit < 0))
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
    const bool same = juce::exactlyEqual (c.shiftMs, shown.shiftMs) && c.invert == shown.invert && c.rotate == shown.rotate
                   && juce::exactlyEqual (c.rotateHz, shown.rotateHz) && juce::exactlyEqual (c.rotateQ, shown.rotateQ)
                   && bypass == shownBypass;
    if (same && ! dirty) return;
    shown = c;
    shownBypass = bypass;
    dirty = false;
    fixed.resize (raw.size());
    if (bypass) fixed = raw;
    else if (! raw.empty()) pf::applyCorrection (raw.data(), (int) raw.size(), c, fs, fixed.data());
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
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

    // Grid: minor and major time lines, centre and half-scale lines.
    const float step = gridStep (span);
    g.setColour (col::grid);
    for (float t = std::ceil (t0 / (step / 5.f)) * (step / 5.f); t <= t0 + span; t += step / 5.f)
        g.fillRect (xAt (t), plot.getY(), 1.f, plot.getHeight());
    for (float frac : { 0.25f, 0.75f })
        g.fillRect (plot.getX(), plot.getY() + plot.getHeight() * frac, plot.getWidth(), 1.f);
    g.setColour (col::hairline);
    for (float t = std::ceil (t0 / step) * step; t <= t0 + span + 1e-4f; t += step)
        g.fillRect (xAt (t), plot.getY(), 1.f, plot.getHeight());
    g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.f);
    g.drawRect (plot, 1.f);

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

    // Visible samples, and one scale per signal (track before/after share theirs).
    const auto idx = [&] (float ms) { return (double) (hit - start) + ms * 0.001 * fs; };
    const int i0 = std::max (0, (int) std::floor (idx (t0)) - 1);
    const int i1 = std::min ((int) ref.size() - 1, (int) std::ceil (idx (t0 + span)) + 1);
    float pr = 1e-6f, pt = 1e-6f;
    for (int i = i0; i <= i1; ++i)
    {
        pr = std::max (pr, std::abs (ref[(size_t) i]));
        pt = std::max ({ pt, std::abs (fixed[(size_t) i]), std::abs (raw[(size_t) i]) });
    }
    const float half = plot.getHeight() * 0.5f * 0.86f;
    const auto xOf = [&] (int i) { return xAt ((float) ((i - (double) (hit - start)) / fs * 1000.0)); };
    const auto yOf = [&] (float v, float peak) { return plot.getCentreY() - v / peak * half; };

    const auto trace = [&] (const std::vector<float>& s, float peak) {
        juce::Path p;
        for (int i = i0; i <= i1; ++i)
        {
            const juce::Point<float> at (xOf (i), yOf (s[(size_t) i], peak));
            if (i == i0) p.startNewSubPath (at); else p.lineTo (at);
        }
        return p;
    };

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.reduced (1.f).toNearestInt());

    // What cancels: the area between reference and corrected track.
    {
        juce::Path between;
        between.startNewSubPath (xOf (i0), yOf (ref[(size_t) i0], pr));
        for (int i = i0 + 1; i <= i1; ++i) between.lineTo (xOf (i), yOf (ref[(size_t) i], pr));
        for (int i = i1; i >= i0; --i) between.lineTo (xOf (i), yOf (fixed[(size_t) i], pt));
        between.closeSubPath();
        between.setUsingNonZeroWinding (false);
        g.setColour (col::accentWash);
        g.fillPath (between);
        juce::Graphics::ScopedSaveState s2 (g);
        g.reduceClipRegion (between);
        hatch (g, plot, col::accent.withAlpha (0.28f), 4.f, 1.f);
    }

    // Before: dotted.
    {
        juce::Path dots;
        const float dash[] = { 1.5f, 2.5f };
        juce::PathStrokeType (1.2f).createDashedStroke (dots, trace (raw, pt), dash, 2);
        g.setColour (col::accent.withAlpha (0.55f));
        g.fillPath (dots);
    }
    g.setColour (col::ink);
    g.strokePath (trace (ref, pr), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::accent);
    g.strokePath (trace (fixed, pt), juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Hit marker.
    const float hx = xAt (0.f);
    juce::Path tri;
    tri.addTriangle (hx - 4.f, plot.getY() + 1.f, hx + 4.f, plot.getY() + 1.f, hx, plot.getY() + 6.f);
    g.setColour (col::ink);
    g.fillPath (tri);

    // Readout and hints.
    g.setFont (mono (10.f));
    g.setColour (col::inkSoft);
    const auto info = juce::String ("ZOOM ") + theme::fixed (span, span < 10.f ? 1 : 0) + " ms";
    g.drawText (info, plot.reduced (8.f, 6.f), juce::Justification::topLeft, false);
    if (isMouseOver() && ! dragging)
        caption (g, juce::String (juce::CharPointer_UTF8 ("Drag to nudge \xc2\xb7 Shift fine \xc2\xb7 Wheel to zoom")),
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
    dragShift = proc.param (ids::shift).convertFrom0to1 (proc.param (ids::shift).getValue());
    shiftAttachment.beginGesture();
}

void Scope::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dx = e.position.x - lastX;
    lastX = e.position.x;
    dragShift += dx / plotArea().getWidth() * zoomMs * (e.mods.isShiftDown() ? 0.1f : 1.f);
    dragShift = juce::jlimit (-(float) pf::limits::kMaxShiftMs, (float) pf::limits::kMaxShiftMs, dragShift);
    shiftAttachment.setValueAsPartOfGesture (dragShift);
    recompute();
}

void Scope::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    shiftAttachment.endGesture();
    repaint();
}

void Scope::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (plotArea().contains (e.position) && ! holdArea().contains (e.position))
    {
        zoomMs = 12.f;
        repaint();
    }
}

void Scope::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    zoomMs = juce::jlimit (kMinZoom, kMaxZoom, zoomMs * std::pow (1.25f, -d * 4.f * (w.isReversed ? -1.f : 1.f)));
    repaint();
}
