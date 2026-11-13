#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/ExportKey.h"
#include "ui/MapLane.h"
#include "ui/PhaseStrip.h"
#include "ui/Scope.h"

// One sheet, laid out at 880 x 620 and scaled as a whole when the window is
// resized (fixed aspect):
//   header   wordmark, input meters, ALIGN
//   A        scope
//   B        phase / frequency, or in per-hit mode the phase map
//   C        correction: one turn or per hit, phase (offset), polarity, A/B,
//            time anchor, export
//   footer   one line of what's going on
//
// Nothing repaints on a timer unless what it shows changed: the controls are
// opaque and repaint themselves, the sheet only redraws the readout, the
// time-anchor block and the footer when their text changes.
class PhaseFixerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 880, kHeight = 620;

    explicit PhaseFixerEditor (PhaseFixerProcessor&);
    ~PhaseFixerEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

    void tick();   // one UI update (timer; the snapshot tool calls it directly)
    Scope& getScope() noexcept { return scope; }

private:
    struct Sheet : juce::Component
    {
        explicit Sheet (PhaseFixerEditor& e) : editor (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        PhaseFixerEditor& editor;
    };

    void timerCallback() override { tick(); }
    void layout();
    void updateStatus();

    static juce::Rectangle<int> readoutArea() { return { 108, 500, 206, 84 }; }
    static juce::Rectangle<float> readoutNumberArea() { return { 110.f, 506.f, 200.f, 36.f }; }   // the phase dial's number
    juce::String readoutNumber();
    static juce::Rectangle<int> sectionCHeader() { return { 24, 480, 832, 20 }; }
    static juce::Rectangle<int> anchorArea() { return { 512, 498, 344, 90 }; }
    static juce::Rectangle<int> footerArea() { return { 24, 596, 832, 20 }; }

    PhaseFixerProcessor& proc;
    SheetLookAndFeel lnf;
    Sheet sheet { *this };

    LevelMeter refMeter { "Ref" }, trackMeter { "Track" };
    AlignKey alignKey;
    Scope scope { proc };
    PhaseStrip strip { proc };
    MapLane lane { proc };
    TagSwitch modeSwitch;
    ExportKey exportKey { proc };

    Dial phaseDial;
    Key polarityKey, compareKey;
    TextLink resetLink { "Reset" };

    juce::String status, statusRight, anchorKey;
    bool statusWarn = false, perHitShown = false;
    double liveShown = 0.0, mapGainDb = 0.0;
    int mapPoints = 0, mapRevision = -1;
    juce::uint32 seenResult = 0;
    int ticks = 0, refQuietTicks = 0;
};
