#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The house style of the plugin family (the audio-plugin SDK): a printed
// drawing sheet, warm neutrals, ink and hairlines, one accent colour that
// means one thing. No gradients, no shadows, no glow; hairlines and type do
// the work. PhaseFucker runs on the dark "night" sheet.
namespace theme
{
    // Every colour in the UI comes from these roles. Components never write a
    // hex value or call brighter()/darker(): both variants must work from the
    // same drawing code, and brighter() on a light sheet goes the wrong way.
    struct Palette
    {
        juce::uint32 sheet;        // the page; every opaque component fills with it first
        juce::uint32 well;         // inside plots, meter tracks, text entry (= sheet on paper)
        juce::uint32 raised;       // knob discs, popup menus, readout boxes (= sheet on paper)
        juce::uint32 ink;          // primary text, pointers, the reference trace, "on" fills
        juce::uint32 inkSoft;      // secondary text, captions, idle outlines (5.5-7:1 on the sheet)
        juce::uint32 inkFaint;     // tertiary text, axis labels, crop marks, off values (2.4-2.8:1)
        juce::uint32 hairline;     // 1 px rules, plot outlines, major grid (~1.35:1)
        juce::uint32 grid;         // minor grid (~1.1-1.16:1)
        juce::uint32 tick;         // unlit dial ticks
        juce::uint32 rim;          // dial disc outline, popup menu and readout borders
        juce::uint32 rimHot;       // dial disc outline while hovered or dragged
        juce::uint32 headerRule;   // the 1.5 px rule under the masthead
        juce::uint32 footerRule;   // the 1 px rule over the footer
        juce::uint32 accent;       // the ONE signal colour
        juce::uint32 accentDim;    // accent at ~35 % on the sheet: text selection
        juce::uint32 accentWash;   // accent at low alpha: areas (what cancels)
    };

    // Light "paper" variant: the lab paper PhaseFucker shipped with up to v0.3.
    inline constexpr Palette paper {
        0xffeeeae0,   // sheet     paper           OKLCH L .94 C .014 H 89
        0xffeeeae0,   // well      plots sit on the paper itself
        0xffeeeae0,   // raised    discs are paper with an ink rim
        0xff141414,   // ink                       15.3:1
        0xff5d5b56,   // inkSoft                    5.7:1
        0xff9c988e,   // inkFaint                   2.4:1
        0xffcfcabe,   // hairline  ink at ~12 %     1.36:1
        0xffdfdacf,   // grid      ink at ~6 %      1.16:1
        0xffcfcabe,   // tick      = hairline
        0xff141414,   // rim       = ink
        0xff141414,   // rimHot    = ink (the stroke thickens instead)
        0xff141414,   // headerRule ink
        0xff141414,   // footerRule ink
        0xffe2401c,   // accent    vermilion        3.5:1, OKLCH L .61 C .20 H 34
        0xffeaae9b,   // accentDim vermilion 35 % on paper
        0x1fe2401c,   // accentWash vermilion 12 %
    };

    // Dark "night" variant: the SDK's night sheet (BuildApp's), acid accent.
    inline constexpr Palette night {
        0xff131211,   // sheet     warm black      OKLCH L .18 C .003 H 68
        0xff0c0b0a,   // well      below the sheet  1.05:1
        0xff1b1a18,   // raised    knob discs, menus
        0xffece7db,   // ink       bone            15.2:1
        0xffa39d91,   // inkSoft                    6.9:1
        0xff615c54,   // inkFaint                   2.8:1
        0xff2f2c28,   // hairline                   1.35:1
        0xff1d1b19,   // grid                       1.09:1
        0xff585653,   // tick
        0xff6a6865,   // rim
        0xffa39d91,   // rimHot    = inkSoft
        0xff615c54,   // headerRule inkFaint (a bone rule would shout on black)
        0xff2f2c28,   // footerRule hairline
        0xffc8f03c,   // accent    acid            14.2:1, OKLCH L .90 C .20 H 122
        0xff4d5c1a,   // accentDim acid ~35 % on the sheet
        0x1fc8f03c,   // accentWash acid 12 %
    };

    constexpr Palette withAccent (Palette p, juce::uint32 accent, juce::uint32 accentDim, juce::uint32 accentWash)
    {
        p.accent = accent;
        p.accentDim = accentDim;
        p.accentWash = accentWash;
        return p;
    }

    // ACCENT MEANS: the track being corrected, and the states that need you
    // (listening, aligned, warnings, hints). Nothing else on screen may use it.
    // Night orange: the old vermilion's direction, lifted for the dark sheet.
    // OKLCH L .74 C .18 H 55, 7.7:1 on the sheet and for sheet-coloured text on
    // it; OKLab distance >= 0.2 from the siblings' accents (BuildApp acid,
    // VocAnal ultraviolet, Distitten magenta, PsyBox yellow).
    inline constexpr Palette palette = withAccent (night, 0xffff8505, 0xff663a0d, 0x1fff8505);

    namespace col
    {
        inline const juce::Colour sheet      { palette.sheet };
        inline const juce::Colour well       { palette.well };
        inline const juce::Colour raised     { palette.raised };
        inline const juce::Colour ink        { palette.ink };
        inline const juce::Colour inkSoft    { palette.inkSoft };
        inline const juce::Colour inkFaint   { palette.inkFaint };
        inline const juce::Colour hairline   { palette.hairline };
        inline const juce::Colour grid       { palette.grid };
        inline const juce::Colour tick       { palette.tick };
        inline const juce::Colour rim        { palette.rim };
        inline const juce::Colour rimHot     { palette.rimHot };
        inline const juce::Colour headerRule { palette.headerRule };
        inline const juce::Colour footerRule { palette.footerRule };
        inline const juce::Colour accent     { palette.accent };
        inline const juce::Colour accentDim  { palette.accentDim };
        inline const juce::Colour accentWash { palette.accentWash };
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

    // A plot's paper: the well, its grid drawn on top by the caller.
    void plotWell (juce::Graphics& g, juce::Rectangle<float> plot);

    // A reading box over a plot ("120 Hz  −40° → +2°"): raised, rimmed, ink text.
    void readoutBox (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text);

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
