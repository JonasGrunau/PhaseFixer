#include "PhaseStrip.h"

using namespace theme;

namespace
{
    constexpr float kLoHz = 20.f, kHiHz = 20000.f;
    constexpr float kMinWeight = 0.02f;   // quieter bands aren't drawn
}

PhaseStrip::PhaseStrip (PhaseFuckerProcessor& p) : proc (p) { setOpaque (true); }

juce::Rectangle<float> PhaseStrip::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (24.f).withTrimmedBottom (14.f).withTrimmedLeft (34.f);
}

float PhaseStrip::xOf (float hz) const
{
    const auto p = plotArea();
    return p.getX() + p.getWidth() * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz);
}

void PhaseStrip::refresh()
{
    proc.withCapture ([&] (const pf::Capture& cap) { live.update (cap); });
    if (! live.hasData())
    {
        if (! before.empty()) { before.clear(); after.clear(); repaint(); }
        return;
    }
    // Only when something new played or the correction moved.
    const bool bypass = proc.isComparing();
    const auto c = bypass ? pf::Correction {} : proc.correction();
    if (live.revision() == seenRevision && bypass == shownBypass
        && juce::exactlyEqual (c.phaseDeg, shown.phaseDeg) && c.invert == shown.invert)
        return;
    seenRevision = live.revision();
    shown = c;
    shownBypass = bypass;

    const auto& s = live.spectrum();
    s.bands (nullptr, before);
    s.bands (&c, after);
    lowGainDb = (float) s.sumGainDb (c, 20.0, 300.0);
    repaint();
}

void PhaseStrip::paint (juce::Graphics& g)
{
    paintOffscreen (g, getLocalBounds(), buffer, [this] (juce::Graphics& o) { draw (o); });
}

void PhaseStrip::draw (juce::Graphics& g)
{
    g.fillAll (col::paper);
    const auto plot = plotArea();
    const auto yOf = [&] (float deg) { return plot.getCentreY() - deg / 180.f * plot.getHeight() * 0.5f; };

    sectionTag (g, "B", "Phase / frequency", { 0.f, 4.f });
    {
        const auto r = juce::Rectangle<float> ((float) getWidth() - 200.f, 2.f, 200.f, 17.f);
        const bool has = ! before.empty();
        g.setFont (monoStrong (12.f));
        g.setColour (has && lowGainDb > 0.5f ? col::accent : col::ink);
        const auto v = has ? signedNumber (lowGainDb, 1) + " dB" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        g.drawText (v, r, juce::Justification::centredRight, false);
        const float w = textWidth (g.getCurrentFont(), v);
        caption (g, "Low end vs. bypass", r.withTrimmedRight (w + 8.f), juce::Justification::centredRight, col::inkSoft, 9.5f);
    }

    // Cancellation zones (beyond +-90 degrees).
    const auto zoneTop = plot.withBottom (yOf (90.f));
    const auto zoneBottom = plot.withTop (yOf (-90.f));
    g.setColour (col::accentWash.withMultipliedAlpha (0.55f));
    g.fillRect (zoneTop);
    g.fillRect (zoneBottom);
    caption (g, "Cancels", zoneTop.reduced (6.f, 1.f), juce::Justification::topRight, col::accent.withAlpha (0.7f), 8.5f);
    caption (g, "Cancels", zoneBottom.reduced (6.f, 1.f), juce::Justification::bottomRight, col::accent.withAlpha (0.7f), 8.5f);

    // Grid.
    g.setColour (col::grid);
    for (float hz : { 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f,
                      2000.f, 3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f })
        g.fillRect (xOf (hz), plot.getY(), 1.f, plot.getHeight());
    g.setColour (col::hairline);
    for (float hz : { 100.f, 1000.f, 10000.f })
        g.fillRect (xOf (hz), plot.getY(), 1.f, plot.getHeight());
    g.fillRect (plot.getX(), yOf (0.f), plot.getWidth(), 1.f);
    g.drawRect (plot, 1.f);

    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    for (auto [hz, name] : { std::pair { 20.f, "20" }, { 100.f, "100" }, { 1000.f, "1k" }, { 10000.f, "10k" }, { 20000.f, "20k" } })
        g.drawText (name, juce::Rectangle<float> (xOf (hz) - 20.f, plot.getBottom() + 1.f, 40.f, 12.f),
                    hz == 20.f ? juce::Justification::centredLeft : hz == 20000.f ? juce::Justification::centredRight : juce::Justification::centred, false);
    for (auto [deg, name] : { std::pair { 180.f, "180" }, { 90.f, "90" }, { 0.f, "0" }, { -90.f, "-90" }, { -180.f, "-180" } })
        g.drawText (juce::String (name).replace ("-", juce::CharPointer_UTF8 ("\xe2\x88\x92")),
                    juce::Rectangle<float> (0.f, yOf (deg) - 6.f, 30.f, 12.f).withY (juce::jlimit (plot.getY() - 2.f, plot.getBottom() - 10.f, yOf (deg) - 6.f)),
                    juce::Justification::centredRight, false);

    if (before.empty())
    {
        g.setColour (col::inkSoft);
        g.setFont (text (12.f));
        g.drawText ("Waiting for both tracks to play together", plot, juce::Justification::centred, false);
        return;
    }

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.expanded (0.f, 4.f).toNearestInt());

    // As it arrives: faint dots.
    for (const auto& b : before)
        if (b.weight > kMinWeight)
        {
            g.setColour (col::inkFaint.withAlpha (0.3f + 0.6f * b.coherence));
            g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre ({ xOf (b.hz), yOf (b.phaseDeg) }));
        }

    // Corrected: a line through dots sized by level.
    for (size_t i = 1; i < after.size(); ++i)
    {
        const auto& a = after[i - 1];
        const auto& b = after[i];
        if (a.weight < kMinWeight || b.weight < kMinWeight || std::abs (a.phaseDeg - b.phaseDeg) > 180.f) continue;
        g.setColour (col::accent.withAlpha (0.35f + 0.5f * std::min (a.coherence, b.coherence)));
        g.drawLine (xOf (a.hz), yOf (a.phaseDeg), xOf (b.hz), yOf (b.phaseDeg), 1.3f);
    }
    for (const auto& b : after)
        if (b.weight > kMinWeight)
        {
            const float d = 2.f + 4.f * std::sqrt (b.weight);
            g.setColour (col::accent.withAlpha (0.3f + 0.7f * b.coherence));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ xOf (b.hz), yOf (b.phaseDeg) }));
        }

    // Hover reading.
    if (hoverX >= plot.getX() && hoverX <= plot.getRight())
    {
        const float hz = kLoHz * std::pow (kHiHz / kLoHz, (hoverX - plot.getX()) / plot.getWidth());
        size_t best = 0;
        for (size_t i = 1; i < after.size(); ++i)
            if (std::abs (std::log (after[i].hz / hz)) < std::abs (std::log (after[best].hz / hz))) best = i;
        const float x = xOf (after[best].hz);
        g.setColour (col::ink.withAlpha (0.5f));
        g.fillRect (x, plot.getY(), 1.f, plot.getHeight());

        const auto deg = [] (float d) { return signedNumber (d, 0).replace ("+", "") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); };
        const float bhz = after[best].hz;
        const auto txt = (bhz < 1000.f ? fixed (bhz, 0) + " Hz  " : fixed (bhz / 1000.f, 1) + " kHz  ")
                       + deg (before[best].phaseDeg) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + deg (after[best].phaseDeg);
        g.setFont (mono (10.f));
        const float w = textWidth (g.getCurrentFont(), txt) + 12.f;
        auto box = juce::Rectangle<float> (x + 8.f, plot.getY() + 4.f, w, 17.f);
        if (box.getRight() > plot.getRight()) box.setX (x - 8.f - w);
        g.setColour (col::paper);
        g.fillRect (box);
        g.setColour (col::ink);
        g.drawRect (box, 1.f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }
}
