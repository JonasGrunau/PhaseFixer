#pragma once

#include "plugin/PluginProcessor.h"
#include "Theme.h"

// Both waveforms over each other around the last hit they share: reference
// in ink, corrected track in vermilion, the track as it was in vermilion dots,
// and the area between reference and track hatched (what cancels). The
// corrected trace is computed here from the raw capture with the current
// settings, so it follows every knob even with the transport stopped.
//
// Drag sideways to nudge the shift (Shift = fine), wheel to zoom, double-click
// to reset the zoom. The HOLD tag freezes the hit.
class Scope : public juce::Component
{
public:
    explicit Scope (PhaseFuckerProcessor&);

    void refresh();                                   // editor timer
    void showHit (juce::int64 sample, int holdMs);    // jump to the hit an analysis used

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> holdArea() const;
    bool grab (const pf::Capture& cap, juce::int64 hitSample);
    void recompute();

    PhaseFuckerProcessor& proc;
    juce::ParameterAttachment shiftAttachment;

    std::vector<float> ref, raw, fixed;
    juce::int64 start = -1, hit = -1;
    double fs = 48000.0;
    float heldScore = 0.f;
    juce::uint32 lastGrab = 0, holdUntil = 0;
    bool frozen = false;

    pf::Correction shown;
    bool shownBypass = false, dirty = true;

    float zoomMs = 12.f;
    bool dragging = false;
    float lastX = 0.f, dragShift = 0.f;
};
