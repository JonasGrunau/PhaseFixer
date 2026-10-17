#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

// The export tag. Click: record the next pass (it starts when playback starts
// or the loop comes round). While recording, click to stop early. Once the
// file is rendered the tag turns into a handle: drag it into the arrangement.
// Clicking it then records a new pass. Right-click: show the file in Finder.
class ExportKey : public juce::Component
{
public:
    explicit ExportKey (PhaseFuckerProcessor&);

    void refresh();   // editor timer
    juce::uint32 readySince() const noexcept { return readyAt; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    PhaseFuckerProcessor& proc;
    PhaseFuckerProcessor::ExportInfo info;
    juce::String shown;
    juce::uint32 readyAt = 0;
    bool dragged = false, menu = false;
};
