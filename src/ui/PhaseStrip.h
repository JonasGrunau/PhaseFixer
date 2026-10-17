#pragma once

#include "align/LiveSpectrum.h"
#include "plugin/PluginProcessor.h"
#include "Theme.h"

// Phase of the track against the reference across frequency, from what has
// been playing for the last few seconds: dotted = as the track arrives,
// vermilion = with the current correction. Dots grow with the band's level and
// fade where the band doesn't add up coherently. Beyond +-90 degrees the two
// cancel; that zone is shaded. Hover for a reading.
class PhaseStrip : public juce::Component
{
public:
    explicit PhaseStrip (PhaseFuckerProcessor&);

    void refresh();   // editor timer

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent& e) override { hoverX = e.position.x; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1.f; repaint(); }

private:
    void draw (juce::Graphics&);
    juce::Rectangle<float> plotArea() const;
    float xOf (float hz) const;

    PhaseFuckerProcessor& proc;
    pf::LiveSpectrum live;
    std::vector<pf::CrossSpectrum::Band> before, after;
    float lowGainDb = 0.f;
    float hoverX = -1.f;
    juce::Image buffer;
    int seenRevision = -1;
    pf::Correction shown;
    bool shownBypass = false;
};
