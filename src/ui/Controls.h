#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Popup menus and text entry on paper.
class PaperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaperLookAndFeel();
    juce::Font getPopupMenuFont() override { return theme::mono (12.f); }
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    int getPopupMenuBorderSize() override { return 4; }
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

// Every control paints its own paper and is opaque, so repainting one never
// repaints the sheet behind it.

// A measuring dial on a parameter: paper disc, ink ring with fine ticks, a
// pointer with a vermilion tip. Drag up/down (Shift = fine), wheel, double-
// click to type a value, Alt/Cmd-click to reset.
class Dial : public juce::Component
{
public:
    Dial (juce::RangedAudioParameter& p, juce::String name, std::function<juce::String (float)> format);

    void setDimmed (bool d) { dimmed = d; repaint(); }
    void setShowsValue (bool s) { showsValue = s; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    float getValue() const noexcept { return value; }
    std::function<void()> onValueChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::Rectangle<float> discArea() const;
    void nudge (float deltaNorm, bool asGesture);

    juce::RangedAudioParameter& param;
    juce::String name;
    std::function<juce::String (float)> format;
    juce::ParameterAttachment attachment;
    float value = 0.f;
    float lastY = 0.f;
    bool dragging = false, dimmed = false, showsValue = true, bipolar = false;
    std::unique_ptr<juce::TextEditor> entry;
};

// A square key bound to an on/off parameter. On = solid ink (or vermilion
// when it flags an unusual state, like A/B compare).
class Key : public juce::Component
{
public:
    Key (juce::RangedAudioParameter& p, juce::String text, float textHeight = 11.f);

    void setAccentWhenOn (bool a) { accentWhenOn = a; }
    bool isOn() const noexcept { return on; }
    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
    float textHeight;
    juce::ParameterAttachment attachment;
    bool on = false, accentWhenOn = false;
};

// The ALIGN key. Ink block at rest; while listening it turns to paper with a
// vermilion bar filling as shared signal comes in; "ALIGNED" stamp after.
class AlignKey : public juce::Component
{
public:
    AlignKey() { setOpaque (true); }
    enum class Look { idle, listening, analysing, aligned, failed };
    void setLook (Look l, float progress);
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    Look look = Look::idle;
    float progress = 0.f;
};

// Horizontal input meter, -60..0 dBFS, with a peak tick.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String n) : name (std::move (n)) { setOpaque (true); }
    void push (float peakLinear);   // once per UI tick
    void setMissing (bool m, juce::String why = {});
    void paint (juce::Graphics&) override;

private:
    juce::String name, missingText;
    float levelDb = -100.f, holdDb = -100.f, shownLevel = -100.f, shownHold = -100.f;
    int holdTicks = 0;
    bool missing = false;
};

// Small underlined caption that does one thing ("RESET").
class TextLink : public juce::Component
{
public:
    explicit TextLink (juce::String t) : text (std::move (t))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setOpaque (true);
    }
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
};
