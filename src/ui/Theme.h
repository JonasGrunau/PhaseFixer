#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// "Lab paper": warm paper, black ink, one vermilion signal colour. No
// gradients, no shadows, no glow; hairlines and type do the work.
namespace theme
{
    namespace col
    {
        inline const juce::Colour paper     { 0xffeeeae0 };
        inline const juce::Colour ink       { 0xff141414 };
        inline const juce::Colour inkSoft   { 0xff5d5b56 };   // secondary text
        inline const juce::Colour inkFaint  { 0xff9c988e };   // tertiary text, ticks
        inline const juce::Colour hairline  { 0xffcfcabe };   // ink at ~12 % on paper
        inline const juce::Colour grid      { 0xffdfdacf };   // ink at ~6 % on paper
        inline const juce::Colour accent    { 0xffe2401c };   // vermilion
        inline const juce::Colour accentWash { 0x1fe2401c };  // vermilion at 12 %
    }

    // IBM Plex Sans Condensed for labels, IBM Plex Mono for numbers.
    juce::Font label (float height = 10.f);        // uppercase captions, tracked
    juce::Font text (float height = 12.f);
    juce::Font title (float height = 22.f);
    juce::Font mono (float height = 11.f);
    juce::Font monoStrong (float height = 11.f);

    // Uppercase caption with tracking.
    void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                  juce::Justification j = juce::Justification::centredLeft, juce::Colour c = col::inkSoft, float height = 10.f);

    // Section tag like a drawing callout: a boxed letter and a name, "[A] SCOPE".
    void sectionTag (juce::Graphics& g, juce::String letter, const juce::String& name, juce::Point<float> topLeft);

    // Crop marks in the four corners of r, as on a printed sheet.
    void cropMarks (juce::Graphics& g, juce::Rectangle<float> r, float len = 6.f);

    // Diagonal hatching inside the current clip region.
    void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing = 4.f, float thickness = 1.f);

    float textWidth (const juce::Font& f, const juce::String& s);

    // Draws `draw` into `buffer` with JUCE's software renderer at the
    // screen's pixel density, then blits it. For busy plots this is 2-3x
    // faster than drawing the same paths straight through CoreGraphics.
    void paintOffscreen (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Image& buffer,
                         const std::function<void (juce::Graphics&)>& draw);

    // Fixed decimals; 0 really means none (juce::String (x, 0) prints all of them).
    juce::String fixed (double v, int decimals);

    // Proper minus sign and fixed decimals: "−3.417".
    juce::String signedNumber (double v, int decimals);

    constexpr float kRadius = 2.f;   // the only corner radius anywhere
}
