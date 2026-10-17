#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/PhaseStrip.h"
#include "ui/Scope.h"

// One sheet, laid out at 880 x 620 and scaled as a whole when the window is
// resized (fixed aspect):
//   header   wordmark, input meters, ALIGN
//   A        scope
//   B        phase / frequency
//   C        correction: shift, polarity, rotate, A/B, lookahead
//   footer   one line of what's going on
class PhaseFuckerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 880, kHeight = 620;

    explicit PhaseFuckerEditor (PhaseFuckerProcessor&);
    ~PhaseFuckerEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

    void tick();   // one UI update (timer; the snapshot tool calls it directly)

private:
    struct Sheet : juce::Component
    {
        explicit Sheet (PhaseFuckerEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        PhaseFuckerEditor& editor;
    };

    void timerCallback() override { tick(); }
    void layout();
    void updateStatus();

    PhaseFuckerProcessor& proc;
    PaperLookAndFeel lnf;
    Sheet sheet { *this };

    LevelMeter refMeter { "Ref" }, trackMeter { "Track" };
    AlignKey alignKey;
    Scope scope { proc };
    PhaseStrip strip { proc };

    Dial shiftDial, freqDial, qDial;
    Key polarityKey, rotateKey, compareKey;
    LookaheadField lookahead;
    TextLink resetLink { "Reset" };

    juce::String status, statusRight;
    bool statusWarn = false;
    juce::uint32 seenResult = 0;
    int ticks = 0, refQuietTicks = 0;
};
