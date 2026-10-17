#include "Theme.h"

#include "BinaryData.h"

namespace theme
{
namespace
{
    struct Typefaces
    {
        juce::Typeface::Ptr condMedium, condSemi, mono, monoMedium;
        Typefaces()
        {
            condMedium = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansCondensedMedium_ttf, BinaryData::IBMPlexSansCondensedMedium_ttfSize);
            condSemi = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexSansCondensedSemiBold_ttf, BinaryData::IBMPlexSansCondensedSemiBold_ttfSize);
            mono = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
            monoMedium = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
        }
    };

    const Typefaces& faces()
    {
        static const Typefaces t;
        return t;
    }

    juce::Font make (const juce::Typeface::Ptr& tf, float h, float tracking = 0.f)
    {
        return juce::Font (juce::FontOptions (tf).withHeight (h).withKerningFactor (tracking));
    }
} // namespace

juce::Font label (float h) { return make (faces().condSemi, h, 0.12f); }
juce::Font text (float h) { return make (faces().condMedium, h, 0.01f); }
juce::Font title (float h) { return make (faces().condSemi, h, -0.01f); }
juce::Font mono (float h) { return make (faces().mono, h); }
juce::Font monoStrong (float h) { return make (faces().monoMedium, h); }

void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area, juce::Justification j, juce::Colour c, float height)
{
    g.setColour (c);
    g.setFont (label (height));
    g.drawText (s.toUpperCase(), area, j, false);
}

void sectionTag (juce::Graphics& g, juce::String letter, const juce::String& name, juce::Point<float> p)
{
    const juce::Rectangle<float> box (p.x, p.y, 13.f, 13.f);
    g.setColour (col::ink);
    g.drawRect (box, 1.f);
    g.setFont (label (9.f));
    g.drawText (letter, box.translated (0.5f, 0.f), juce::Justification::centred, false);
    caption (g, name, { box.getRight() + 6.f, p.y - 1.f, 200.f, 15.f }, juce::Justification::centredLeft, col::ink, 10.f);
}

void cropMarks (juce::Graphics& g, juce::Rectangle<float> r, float len)
{
    g.setColour (col::inkFaint);
    const auto mark = [&] (float x, float y, float dx, float dy) {
        g.drawLine (x, y, x + dx * len, y, 1.f);
        g.drawLine (x, y, x, y + dy * len, 1.f);
    };
    mark (r.getX(), r.getY(), 1.f, 1.f);
    mark (r.getRight(), r.getY(), -1.f, 1.f);
    mark (r.getX(), r.getBottom(), 1.f, -1.f);
    mark (r.getRight(), r.getBottom(), -1.f, -1.f);
}

void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing, float thickness)
{
    g.setColour (c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), thickness);
}

float textWidth (const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth (f, s);
}

juce::String fixed (double v, int decimals)
{
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}

juce::String signedNumber (double v, int decimals)
{
    const auto body = fixed (std::abs (v), decimals);
    if (body.containsOnly ("0.")) return body;   // no "−0.000"
    return (v < 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String ("+")) + body;
}

} // namespace theme
