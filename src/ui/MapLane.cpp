#include "MapLane.h"

using namespace theme;

namespace
{
    const juce::String kDeg (juce::CharPointer_UTF8 ("\xc2\xb0"));
    const juce::String kDot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    juce::String degrees (double v, int decimals) { return signedNumber (v, decimals) + kDeg; }
} // namespace

MapLane::MapLane (PhaseFixerProcessor& p) : proc (p) { setOpaque (true); }

juce::Rectangle<float> MapLane::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (14.f).withTrimmedLeft (34.f);
}

juce::Rectangle<float> MapLane::clearArea() const
{
    return { (float) getWidth() - 46.f, 2.f, 46.f, 17.f };
}

float MapLane::xOf (double ppq) const
{
    const auto p = plotArea();
    return p.getX() + (float) ((ppq - fromPpq) / (toPpq - fromPpq)) * p.getWidth();
}

float MapLane::yOf (double deg) const
{
    const auto p = plotArea();
    return p.getCentreY() - (float) (deg / 180.0) * (p.getHeight() * 0.5f - 4.f);
}

void MapLane::fitRange()
{
    // Whole bars around the hits.
    if (map.empty())
    {
        fromPpq = 0.0;
        toPpq = barLength;
        return;
    }
    fromPpq = std::floor (map.points.front().ppq / barLength + 1e-6) * barLength;
    toPpq = std::max (fromPpq + barLength, std::ceil (map.points.back().ppq / barLength + 1e-6) * barLength);
}

void MapLane::refresh()
{
    bool changed = false;
    const auto t = proc.getTransport();
    if (t.valid)
    {
        const double bar = t.numerator * 4.0 / t.denominator, beat = 4.0 / t.denominator;
        if (! juce::exactlyEqual (bar, barLength) || ! juce::exactlyEqual (beat, beatLength))
        {
            barLength = bar;
            beatLength = beat;
            fitRange();
            changed = true;
        }
        bpm = t.bpm;
        const double ls = t.looping ? t.loopStart : 0.0, le = t.looping ? t.loopEnd : 0.0;
        if (! juce::exactlyEqual (ls, loopStart) || ! juce::exactlyEqual (le, loopEnd))
        {
            loopStart = ls;
            loopEnd = le;
            changed = true;
        }
    }
    if (proc.getMapRevision() != seenRevision && dragIndex < 0)
    {
        seenRevision = proc.getMapRevision();
        map = proc.getMap();
        fitRange();
        changed = true;
    }

    // What is heard trails what the plugin is fed by its latency.
    const double play = t.valid ? t.ppq - proc.getLatencyMs() * 0.001 * t.bpm / 60.0 : -1.0;
    if (std::abs (xOf (play) - xOf (playPpq)) >= 1.f || (play < 0.0) != (playPpq < 0.0))
    {
        playPpq = play;
        changed = true;
    }
    if (const int live = proc.getLiveMapIndex(); live != liveIndex)
    {
        liveIndex = live;
        changed = true;
    }
    if (changed) repaint();
}

int MapLane::pointAt (juce::Point<float> at) const
{
    int best = -1;
    float bestDist = 9.f;
    for (int i = 0; i < map.size(); ++i)
    {
        const auto& p = map.points[(size_t) i];
        const float dx = std::abs (xOf (p.ppq) - at.x), dy = std::abs (yOf (p.deg) - at.y);
        const float d = dx + 0.25f * dy;   // mostly by time: a point is easy to catch anywhere above or below it
        if (dx < 9.f && d < bestDist)
        {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

void MapLane::paint (juce::Graphics& g)
{
    paintOffscreen (g, getLocalBounds(), buffer, [this] (juce::Graphics& o) { draw (o); });
}

void MapLane::draw (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto plot = plotArea();

    sectionTag (g, "B", "Phase map", { 0.f, 4.f });

    // Header: how many hits, how many edited, and what the turns add on average.
    if (! map.empty())
    {
        int edited = 0;
        double gain = 0.0;
        for (const auto& p : map.points)
        {
            edited += p.edited ? 1 : 0;
            gain += p.gainDb;
        }
        gain /= map.size();
        auto r = juce::Rectangle<float> (0.f, 2.f, clearArea().getX() - 14.f, 17.f);
        g.setFont (monoStrong (12.f));
        g.setColour (gain > 0.5 ? col::accent : col::ink);
        const auto v = signedNumber (gain, 1) + " dB";
        g.drawText (v, r, juce::Justification::centredRight, false);
        r.removeFromRight (textWidth (g.getCurrentFont(), v) + 8.f);
        caption (g, "Low end per hit", r, juce::Justification::centredRight, col::inkSoft, 9.5f);
        g.setFont (label (9.5f));
        r.removeFromRight (textWidth (g.getCurrentFont(), "LOW END PER HIT") + 22.f);
        caption (g, juce::String (map.size()) + (map.size() == 1 ? " hit" : " hits") + (edited > 0 ? kDot + juce::String (edited) + " edited" : juce::String()),
                 r, juce::Justification::centredRight, col::inkSoft, 9.5f);

        const auto c = clearArea().reduced (1.f);
        g.setColour (col::inkSoft);
        g.drawRoundedRectangle (c, kRadius, 1.f);
        caption (g, "Clear", c, juce::Justification::centred, col::ink, 9.f);
    }

    // Bar numbers 60 px or more apart however long the map is; lines closer than
    // 4 px are left out (a long map would turn grey with them).
    const double span = toPpq - fromPpq;
    const double barPx = plot.getWidth() * barLength / span, beatPx = plot.getWidth() * beatLength / span;
    int every = 1;
    while (every * barPx < 60.0) every *= 2;

    // Grid: beats faint, bars and the zero line stronger.
    plotWell (g, plot);
    {
        juce::Path minor, major;
        for (double b = fromPpq; b <= toPpq + 1e-6; b += beatLength)
        {
            const bool bar = std::abs (std::remainder (b, barLength)) < 1e-6;
            const bool numbered = bar && std::lround (b / barLength) % every == 0;
            if (bar ? barPx >= 4.0 || numbered : beatPx >= 4.0)
                (bar ? major : minor).addRectangle (xOf (b), plot.getY(), 1.f, plot.getHeight());
        }
        for (double d : { -90.0, 90.0 })
            minor.addRectangle (plot.getX(), yOf (d), plot.getWidth(), 1.f);
        major.addRectangle (plot.getX(), yOf (0.0), plot.getWidth(), 1.f);
        g.setColour (col::grid);
        g.fillPath (minor);
        g.setColour (col::hairline);
        g.fillPath (major);
        g.drawRect (plot, 1.f);
    }

    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    for (auto [deg, name] : { std::pair { 180.0, "180" }, { 90.0, "90" }, { 0.0, "0" }, { -90.0, "-90" }, { -180.0, "-180" } })
        g.drawText (juce::String (name).replace ("-", juce::CharPointer_UTF8 ("\xe2\x88\x92")),
                    juce::Rectangle<float> (0.f, yOf (deg) - 6.f, 30.f, 12.f), juce::Justification::centredRight, false);
    for (double b = fromPpq; b < toPpq - 1e-6; b += barLength)
    {
        const int number = (int) std::lround (b / barLength) + 1;
        if ((number - 1) % every != 0) continue;
        g.drawText (juce::String (number), juce::Rectangle<float> (xOf (b) + 3.f, plot.getBottom() + 1.f, 40.f, 12.f),
                    juce::Justification::centredLeft, false);
    }

    if (map.empty())
    {
        g.setColour (col::inkSoft);
        g.setFont (text (12.f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Loop the part, press play, then ALIGN \xe2\x80\x94 every hit of the reference gets its own turn.")),
                    plot, juce::Justification::centred, false);
        return;
    }

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.expanded (0.f, 2.f).toNearestInt());

    // The turn over time, one point per pixel; a jump across +-180 breaks the line.
    {
        juce::Path line;
        const int cols = (int) plot.getWidth();
        double last = 0.0;
        for (int c = 0; c <= cols; ++c)
        {
            const double ppq = fromPpq + (toPpq - fromPpq) * c / cols;
            const double a = map.angleAt (ppq, bpm, loopStart, loopEnd);
            const juce::Point<float> at (plot.getX() + (float) c, yOf (a));
            if (c == 0 || std::abs (a - last) > 180.0) line.startNewSubPath (at); else line.lineTo (at);
            last = a;
        }
        g.setColour (col::accent);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    // Playhead.
    if (playPpq >= fromPpq && playPpq <= toPpq)
    {
        g.setColour (col::ink.withAlpha (0.55f));
        g.fillRect (xOf (playPpq), plot.getY(), 1.f, plot.getHeight());
    }

    // Points, with their turn written next to them where there's room.
    g.setFont (mono (9.f));
    for (int i = 0; i < map.size(); ++i)
    {
        const auto& p = map.points[(size_t) i];
        const float x = xOf (p.ppq), y = yOf (p.deg);
        const float prev = i > 0 ? xOf (map.points[(size_t) i - 1].ppq) : -1e9f;
        const float next = i + 1 < map.size() ? xOf (map.points[(size_t) i + 1].ppq) : 1e9f;
        const float room = std::min (x - prev, next - x);
        if (p.edited)
        {
            g.setColour (col::ink.withAlpha (0.5f));
            g.fillRect (x - 3.f, yOf (p.learnedDeg) - 0.5f, 6.f, 1.f);
        }
        if (i == liveIndex)
        {
            g.setColour (col::ink);
            g.drawEllipse (juce::Rectangle<float> (13.f, 13.f).withCentre ({ x, y }), 1.f);
        }
        // Dense maps (a whole song) get smaller dots, so they don't run into one line.
        const float d = i == hover || i == dragIndex ? 9.f : juce::jlimit (3.f, 7.f, room - 1.f);
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ x, y });
        g.setColour (p.edited ? col::well : col::accent);
        g.fillEllipse (dot);
        if (p.edited)
        {
            g.setColour (col::accent);
            g.drawEllipse (dot.reduced (0.75f), 1.5f);
        }

        if (room >= 34.f || i == hover || i == dragIndex)
        {
            const bool below = y < plot.getY() + 16.f;
            const auto box = juce::Rectangle<float> (44.f, 12.f).withCentre ({ x, below ? y + 12.f : y - 12.f });
            g.setColour (col::ink);
            g.drawText (degrees (p.deg, 0), box.withX (juce::jlimit (plot.getX() + 1.f, plot.getRight() - 45.f, box.getX())),
                        juce::Justification::centred, false);
        }
    }

    // Reading for the point under the mouse.
    if (const int i = dragIndex >= 0 ? dragIndex : hover; i >= 0 && i < map.size())
    {
        const auto& p = map.points[(size_t) i];
        const double beat = (p.ppq - std::floor (p.ppq / barLength + 1e-9) * barLength) / beatLength + 1.0;
        auto txt = "BAR " + juce::String ((int) std::floor (p.ppq / barLength + 1e-9) + 1) + "." + fixed (beat, 2) + "  " + degrees (p.deg, 1);
        if (p.edited) txt += "  (found " + degrees (p.learnedDeg, 1) + ")";
        txt += "  low end " + signedNumber (p.gainDb, 1) + " dB";
        g.setFont (mono (10.f));
        const float w = textWidth (g.getCurrentFont(), txt) + 12.f;
        const float x = xOf (p.ppq);
        auto box = juce::Rectangle<float> (x + 10.f, plot.getBottom() - 21.f, w, 17.f);
        if (box.getRight() > plot.getRight()) box.setX (x - 10.f - w);
        readoutBox (g, box, txt);
    }
}

void MapLane::mouseMove (const juce::MouseEvent& e)
{
    const int h = pointAt (e.position);
    setMouseCursor (h >= 0 ? juce::MouseCursor::UpDownResizeCursor
                           : ! map.empty() && clearArea().contains (e.position) ? juce::MouseCursor::PointingHandCursor
                                                                                 : juce::MouseCursor::NormalCursor);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void MapLane::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0)
    {
        hover = -1;
        repaint();
    }
}

void MapLane::mouseDown (const juce::MouseEvent& e)
{
    if (! map.empty() && clearArea().contains (e.position))
    {
        proc.clearMap();
        hover = -1;
        refresh();
        return;
    }
    dragIndex = pointAt (e.position);
    if (dragIndex < 0) return;
    dragY = e.position.y;
    dragDeg = map.points[(size_t) dragIndex].deg;
}

void MapLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || dragIndex >= map.size()) return;
    // The plot's height is a full turn; wraps round like the phase dial.
    const float span = plotArea().getHeight() - 8.f;
    dragDeg = pf::wrapDeg (dragDeg - (e.position.y - dragY) / span * 360.0 * (e.mods.isShiftDown() ? 0.1 : 1.0));
    dragY = e.position.y;
    map.edit (dragIndex, dragDeg);
    proc.editMapPoint (map.points[(size_t) dragIndex].ppq, dragDeg);
    repaint();
}

void MapLane::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex < 0) return;
    dragIndex = -1;
    refresh();
    repaint();
}

void MapLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int i = pointAt (e.position); i >= 0)
    {
        proc.editMapPoint (map.points[(size_t) i].ppq, map.points[(size_t) i].learnedDeg);
        refresh();
    }
}
