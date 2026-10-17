#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

// Section B in per-hit mode: the phase map along the host's timeline. Every
// hit of the reference is a point at its turn (-180..180 degrees); the
// vermilion line is the turn the track gets over time, holds and glides
// included. Bars are numbered as the host counts them, and a hairline follows
// playback.
//
// Drag a point up or down to set its turn (Shift = fine), double-click it to
// go back to what ALIGN found. An edited point is drawn hollow, with a tick
// where ALIGN had it.
class MapLane : public juce::Component
{
public:
    explicit MapLane (PhaseFuckerProcessor&);

    void refresh();   // editor timer

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void draw (juce::Graphics&);
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> clearArea() const;
    float xOf (double ppq) const;
    float yOf (double deg) const;
    int pointAt (juce::Point<float>) const;
    void fitRange();

    PhaseFuckerProcessor& proc;
    pf::PhaseMap map;
    int seenRevision = -1, liveIndex = -1, hover = -1, dragIndex = -1;
    double fromPpq = 0.0, toPpq = 4.0, barLength = 4.0, beatLength = 1.0, bpm = 120.0;
    double playPpq = -1.0;   // what is heard now, -1 while stopped
    float dragY = 0.f;
    double dragDeg = 0.0;
    juce::Image buffer;
};
