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
    g.setColour (col::inkSoft);
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
    juce::Path p;   // one stroke for all the lines
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
    {
        p.startNewSubPath (x, r.getBottom());
        p.lineTo (x + r.getHeight(), r.getY());
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (thickness));
}

void plotWell (juce::Graphics& g, juce::Rectangle<float> plot)
{
    g.setColour (col::well);
    g.fillRect (plot);
}

void readoutBox (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& s)
{
    g.setColour (col::raised);
    g.fillRect (box);
    g.setColour (col::rim);
    g.drawRect (box, 1.f);
    g.setColour (col::ink);
    g.setFont (mono (10.f));
    g.drawText (s, box, juce::Justification::centred, false);
}

void paintOffscreen (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Image& buffer,
                     const std::function<void (juce::Graphics&)>& draw)
{
    const float scale = std::max (1.f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::roundToInt ((float) bounds.getWidth() * scale), h = juce::roundToInt ((float) bounds.getHeight() * scale);
    if (w <= 0 || h <= 0) return;
    if (buffer.getWidth() != w || buffer.getHeight() != h)
        buffer = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    {
        juce::Graphics off (buffer);
        off.addTransform (juce::AffineTransform::scale ((float) w / (float) bounds.getWidth(), (float) h / (float) bounds.getHeight()));
        draw (off);
    }
    g.drawImageTransformed (buffer, juce::AffineTransform::scale ((float) bounds.getWidth() / (float) w, (float) bounds.getHeight() / (float) h)
                                        .translated ((float) bounds.getX(), (float) bounds.getY()));
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
