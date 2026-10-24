#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

// Both waveforms over each other: reference in ink, corrected track in
// the accent, the track as it was in accent dots, and the area between
// reference and track hatched (what cancels).
//
// What the window starts on (the trigger), switchable in the header:
//   BEAT / BAR  the latest beat or bar of the DAW's timeline, so the picture
//               stands still while the song plays. Without a running host
//               transport it falls back to hits.
//   HIT         the strongest transient both tracks share.
//
// The corrected trace is computed here from the raw capture: the Hilbert
// component of a grab is computed once, after which any angle is one
// multiply-add per sample, so it follows the knob even with the transport
// stopped. Per hit, the angle follows the phase map along the timeline, and
// each hit of the map is marked with its turn. Drawing is reduced to one
// min/max pair per pixel column. The left edge reads the reference's level
// in dBFS (the track is drawn at the reference's level).
//
// How much it shows: BARS (1/4 to 4 bars, following the tempo; 1/2 by
// default, two kicks and what the bass does between them), or any length the
// wheel zooms to (2 ms - 8 s). Double-click goes back to the bar length.
// The height is measured over the bar length (or the zoomed window, when
// wider), once per grab, so zooming and turning don't move it.
//
// Drag sideways over the plot to turn the phase (Shift = fine). HOLD freezes
// the picture.
class Scope : public juce::Component
{
public:
    explicit Scope (PhaseFuckerProcessor&);

    void refresh();                                   // editor timer
    void showHit (juce::int64 sample, int holdMs);    // jump to the hit an analysis used (HIT trigger)
    void setZoom (float ms);    // free zoom, in ms
    void setBars (float bars);  // window length in bars

    static constexpr float kMinZoom = 2.f, kMaxZoom = 8000.f, kMaxSpan = 12000.f;
    static constexpr float kBarChoices[] = { 0.25f, 0.5f, 1.f, 2.f, 4.f };

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    using Trigger = PhaseFuckerProcessor::Trigger;

    void draw (juce::Graphics&);
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> holdArea() const;
    juce::Rectangle<float> triggerArea (int i) const;   // HIT, BEAT, BAR tags
    juce::Rectangle<float> barsArea (int i) const;      // 1/4 ... 4 bars tags
    float span() const;                                 // ms the window shows
    float barSpan (float bars) const;                   // ms of that many bars at the tempo
    float grabSpan() const;                             // ms a grab holds: at least the bar length
    float startOffset (float span) const;               // ms shown before the anchor
    float startOffset() const { return startOffset (span()); }
    bool grab (const pf::Capture& cap, juce::int64 anchor, bool findOnset);
    void recompute();
    double ppqAt (double index) const;      // timeline position of a grabbed sample, NaN if unknown
    double indexOf (double ppq) const;      // the reverse, -1 if not in the grab

    PhaseFuckerProcessor& proc;
    juce::ParameterAttachment phaseAttachment;

    std::vector<float> ref, raw, hil, fixed;
    std::vector<double> grabPpq;            // timeline at every Capture::kBlock samples of the grab
    juce::int64 start = -1, anchor = -1, grabbed = -1;   // grabbed: the anchor asked for (before onset search)
    double fs = 48000.0;
    float heldScore = 0.f, grabbedSpan = 0.f;
    float level = 0.f, match = 1.f;         // the height: what fills the plot, and the track's gain to the reference's level
    juce::uint32 lastGrab = 0, holdUntil = 0;
    bool frozen = false, fallback = false;

    pf::Correction shown;
    bool shownBypass = false, dirty = true, shownPerHit = false;
    int shownRevision = -1;
    double grabLiveTurn = 0.0;
    pf::PhaseMap map;                       // per hit: the map the trace was turned with

    juce::Image buffer;
    float zoomMs = 1000.f, lastBars = 0.5f;
    double knownBpm = 120.0, knownBar = 4.0;   // last tempo and bar length (quarters) the host gave
    bool dragging = false;
    float lastX = 0.f, dragPhase = 0.f;
};
