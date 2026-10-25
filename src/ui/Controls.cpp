#include "Controls.h"

using namespace theme;

// ---- SheetLookAndFeel ----------------------------------------------------------

SheetLookAndFeel::SheetLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::sheet);
    setColour (juce::PopupMenu::backgroundColourId, col::raised);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::hairline);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::TextEditor::backgroundColourId, col::well);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, col::inkSoft);
    setColour (juce::TextEditor::focusedOutlineColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::accentDim);
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    setColour (juce::CaretComponent::caretColourId, col::accent);
}

void SheetLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::raised);
    g.setColour (col::rim);
    g.drawRect (0, 0, w, h, 1);
}

void SheetLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::hairline);
        g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (2, 0);
    if (isHighlighted)
    {
        g.setColour (col::hairline);
        g.fillRect (r);
    }
    g.setColour (col::ink);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (22), juce::Justification::centredLeft, false);
    if (isTicked)
    {
        g.setColour (col::accent);
        g.fillRect (juce::Rectangle<float> (5.f, 5.f).withCentre ({ (float) r.getX() + 11.f, (float) r.getCentreY() }));
    }
}

void SheetLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (over || dragging ? col::ink : col::inkFaint);
    for (float i = 0.25f; i < 1.f; i += 0.25f)
        g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.f);
}

// ---- Dial ----------------------------------------------------------------------

Dial::Dial (juce::RangedAudioParameter& p, juce::String n, std::function<juce::String (float)> f)
    : param (p), name (std::move (n)), format (std::move (f)),
      attachment (p, [this] (float v) { value = v; repaint(); if (onValueChange) onValueChange(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (false);
    setOpaque (true);
}

juce::Rectangle<float> Dial::discArea() const
{
    const auto b = getLocalBounds().toFloat();
    const float below = showsValue ? 30.f : 16.f;
    const float d = std::min (b.getWidth(), b.getHeight() - below);
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() + d * 0.5f });
}

juce::Rectangle<float> Dial::valueArea() const
{
    // 2 px gap, the 13 px name line, then the 15 px value line.
    return getLocalBounds().toFloat().withTop (discArea().getBottom() + 15.f).withHeight (15.f);
}

void Dial::resized()
{
    if (entry != nullptr && entry->getParentComponent() == this)
        entry->setBounds (valueArea().withSizeKeepingCentre ((float) getWidth() - 4.f, 18.f).toNearestInt().constrainedWithin (getLocalBounds()));
}

void Dial::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto area = discArea();
    const auto c = area.getCentre();
    const float R = area.getWidth() * 0.5f;
    const float norm = param.convertTo0to1 (value);
    const float origin = bipolar ? 0.5f : 0.f;
    const bool hot = isMouseOverOrDragging() && ! dimmed;
    const auto ink = dimmed ? col::inkFaint : col::ink;
    const auto angle = [] (float t) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * t); };
    const auto at = [&] (float a, float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };

    // Tick ring: the span between origin and value is inked. Two paths, two strokes.
    constexpr int kTicks = 40;
    juce::Path lit, unlit;
    for (int i = 0; i <= kTicks; ++i)
    {
        const float t = (float) i / kTicks;
        const bool major = i % 10 == 0;
        const bool on = (t >= std::min (origin, norm) - 1e-4f && t <= std::max (origin, norm) + 1e-4f);
        const float a = angle (t);
        auto& p = on ? lit : unlit;
        p.startNewSubPath (at (a, R * (major ? 0.78f : 0.84f)));
        p.lineTo (at (a, R * 0.97f));
    }
    g.setColour (col::tick);
    g.strokePath (unlit, juce::PathStrokeType (1.f));
    g.setColour (ink);
    g.strokePath (lit, juce::PathStrokeType (1.1f));

    // Disc.
    const float rd = R * 0.68f;
    g.setColour (col::raised);
    g.fillEllipse (juce::Rectangle<float> (2 * rd, 2 * rd).withCentre (c));
    g.setColour (dimmed ? col::inkFaint : hot ? col::rimHot : col::rim);
    g.drawEllipse (juce::Rectangle<float> (2 * rd, 2 * rd).withCentre (c), hot ? 1.8f : 1.25f);

    // Pointer with an accent tip.
    const float a = angle (norm);
    g.setColour (ink);
    g.drawLine ({ at (a, rd * 0.18f), at (a, rd * 0.72f) }, 2.f);
    g.setColour (dimmed ? col::inkFaint : col::accent);
    g.drawLine ({ at (a, rd * 0.72f), at (a, rd * 0.95f) }, 2.f);

    // Name and value.
    auto text = getLocalBounds().toFloat().withTop (area.getBottom() + 2.f);
    caption (g, name, text.removeFromTop (13.f), juce::Justification::centredTop, dimmed ? col::inkFaint : col::inkSoft, 9.5f);
    if (showsValue && entry == nullptr)
    {
        g.setColour (dimmed ? col::inkFaint : col::ink);
        g.setFont (mono (11.f));
        g.drawText (format (value), text.removeFromTop (15.f), juce::Justification::centredTop, false);
    }
}

void Dial::nudge (float delta, bool asGesture)
{
    const float norm = juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + delta);
    const float plain = param.convertFrom0to1 (norm);
    if (asGesture) attachment.setValueAsPartOfGesture (plain);
    else attachment.setValueAsCompleteGesture (plain);
}

void Dial::resetToDefault()
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Dial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        resetToDefault();
        return;
    }
    dragging = true;
    lastY = (float) e.position.y;
    attachment.beginGesture();
    repaint();
}

void Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge (dy / 220.f * (e.mods.isShiftDown() ? 0.08f : 1.f), true);
}

void Dial::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
    repaint();
}

void Dial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    nudge (d * 0.06f * (e.mods.isShiftDown() ? 0.1f : 1.f), false);
}

void Dial::mouseDoubleClick (const juce::MouseEvent& e)
{
    // The second click opened a drag gesture: close it before writing the value.
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
        repaint();
    }

    // Only the number types a value; anywhere else on the dial resets it.
    const auto v = valueArea();
    const auto number = v.withSizeKeepingCentre (std::max (24.f, textWidth (mono (11.f), format (value)) + 8.f), v.getHeight());
    if (showsValue && number.contains (e.position))
        openEntry (*this, v.withSizeKeepingCentre ((float) getWidth() - 4.f, 18.f).toNearestInt().constrainedWithin (getLocalBounds()),
                   mono (11.f), juce::Justification::centred);
    else
        resetToDefault();
}

void Dial::openEntry (juce::Component& host, juce::Rectangle<int> area, const juce::Font& font, juce::Justification j)
{
    // Already typing: replacing the editor would close the new one as the old one loses focus.
    if (entry != nullptr) return;
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (font);
    entry->setJustification (j);
    // The number without its unit.
    entry->setText (format (value).upToFirstOccurrenceOf (" ", false, false).replace (juce::CharPointer_UTF8 ("\xc2\xb0"), ""), false);
    entry->setIndents (2, 1);
    host.addAndMakeVisible (*entry);
    entry->setBounds (area);
    entry->selectAll();
    entry->grabKeyboardFocus();

    // Never delete the editor inside its own callback: close it asynchronously.
    const auto close = [this] {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Dial> (this)] {
            if (safe == nullptr || safe->entry == nullptr) return;
            if (auto* parent = safe->entry->getParentComponent(); parent != nullptr && parent != safe.getComponent())
                parent->repaint (safe->entry->getBounds());   // the number it covered
            safe->entry.reset();
            safe->repaint();
        });
    };
    entry->onReturnKey = [this, close] {
        auto t = entry->getText().trim().replace (juce::CharPointer_UTF8 ("\xe2\x88\x92"), "-");
        double v = t.getDoubleValue();
        if (t.containsIgnoreCase ("k")) v *= 1000.0;
        if (t.isNotEmpty())
            attachment.setValueAsCompleteGesture ((float) param.getNormalisableRange().snapToLegalValue ((float) v));
        close();
    };
    entry->onEscapeKey = close;
    entry->onFocusLost = close;
    repaint();
}

// ---- Key ---------------------------------------------------------------------------

Key::Key (juce::RangedAudioParameter& p, juce::String t, float h)
    : text (std::move (t)), textHeight (h),
      attachment (p, [this] (float v) { on = v > 0.5f; repaint(); if (onChange) onChange(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
}

void Key::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto fill = accentWhenOn ? col::accent : col::ink;
    if (on)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, kRadius);
    }
    else
    {
        g.setColour (isMouseOver() ? col::ink : col::inkSoft);
        g.drawRoundedRectangle (r, kRadius, 1.f);
    }
    g.setColour (on ? col::sheet : col::ink);
    g.setFont (text.length() <= 2 ? title (textHeight) : label (textHeight));
    g.drawText (text.length() <= 3 ? text : text.toUpperCase(), r, juce::Justification::centred, false);
}

void Key::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (on ? 0.f : 1.f);
}

// ---- TagSwitch -----------------------------------------------------------------------

TagSwitch::TagSwitch (juce::RangedAudioParameter& p, juce::StringArray n)
    : names (std::move (n)),
      attachment (p, [this] (float v) { index = juce::jlimit (0, names.size() - 1, juce::roundToInt (v)); repaint(); if (onChange) onChange(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
}

juce::Rectangle<float> TagSwitch::tagArea (int i) const
{
    const auto f = label (9.f);
    float x = 0.f;
    for (int k = 0;; ++k)
    {
        const float w = textWidth (f, names[k].toUpperCase()) + 18.f;
        if (k == i) return { x, 0.f, w, (float) getHeight() };
        x += w + 2.f;
    }
}

void TagSwitch::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    for (int i = 0; i < names.size(); ++i)
    {
        const auto r = tagArea (i).reduced (1.f);
        const bool on = i == index, hinted = i == hint && ! on;
        g.setColour (on ? col::ink : col::sheet);
        g.fillRoundedRectangle (r, kRadius);
        g.setColour (hinted ? col::accent : on ? col::ink : col::inkSoft);
        g.drawRoundedRectangle (r, kRadius, hinted ? 1.6f : 1.f);
        caption (g, names[i], r, juce::Justification::centred, on ? col::sheet : hinted ? col::accent : col::ink, 9.f);
    }
}

void TagSwitch::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < names.size(); ++i)
        if (tagArea (i).contains (e.position) && i != index)
            attachment.setValueAsCompleteGesture ((float) i);
}

// ---- AlignKey ------------------------------------------------------------------------

void AlignKey::setLook (Look l, float p)
{
    if (l == look && std::abs (p - progress) < 1e-3f && l != Look::listening) return;
    look = l;
    progress = p;
    repaint();
}

void AlignKey::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (isMouseButtonDown()) r = r.translated (0.f, 1.f);
    const bool hover = isMouseOver();

    juce::String t;
    juce::Colour fg;
    switch (look)
    {
        case Look::idle:
            g.setColour (hover ? col::ink.interpolatedWith (col::sheet, 0.12f) : col::ink);   // a shade towards the sheet
            g.fillRoundedRectangle (r, kRadius);
            t = "ALIGN";
            fg = col::sheet;
            break;
        case Look::listening:
        case Look::analysing:
        {
            g.setColour (col::sheet);
            g.fillRoundedRectangle (r, kRadius);
            const auto bar = r.reduced (3.f).removeFromBottom (4.f);
            if (look == Look::listening)
            {
                g.setColour (col::accent);
                g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.f, 1.f, progress)));
                g.setColour (col::hairline);
                g.drawRect (bar, 0.6f);
            }
            else
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (bar.toNearestInt());
                hatch (g, bar, col::accent, 3.f, 1.2f);
            }
            g.setColour (col::ink);
            g.drawRoundedRectangle (r, kRadius, 1.5f);
            const bool blink = (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
            t = look == Look::listening ? juce::String ("LISTENING") : juce::String (juce::CharPointer_UTF8 ("ANALYSING\xe2\x80\xa6"));
            fg = col::ink;
            if (look == Look::listening && blink)
            {
                g.setColour (col::accent);
                g.fillEllipse (juce::Rectangle<float> (5.f, 5.f).withCentre ({ r.getX() + 11.f, r.getCentreY() - 2.f }));
            }
            r = r.withTrimmedBottom (4.f);
            break;
        }
        case Look::aligned:
            g.setColour (col::accent);
            g.fillRoundedRectangle (r, kRadius);
            t = "ALIGNED";
            fg = col::sheet;
            {
                g.setFont (label (14.f));
                const float w = textWidth (g.getCurrentFont(), t);
                const float x = r.getCentreX() + w * 0.5f + 8.f, y = r.getCentreY();
                juce::Path tick;
                tick.startNewSubPath (x, y);
                tick.lineTo (x + 4.f, y + 4.f);
                tick.lineTo (x + 11.f, y - 5.f);
                g.setColour (col::sheet);
                g.strokePath (tick, juce::PathStrokeType (2.f));
                r = r.translated (-8.f, 0.f);
            }
            break;
        case Look::failed:
            g.setColour (col::sheet);
            g.fillRoundedRectangle (r, kRadius);
            g.setColour (col::accent);
            g.drawRoundedRectangle (r, kRadius, 1.5f);
            t = "NO MATCH";
            fg = col::accent;
            break;
    }
    g.setColour (fg);
    g.setFont (label (14.f));
    g.drawText (t, r, juce::Justification::centred, false);
}

void AlignKey::mouseUp (const juce::MouseEvent& e)
{
    repaint();
    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

// ---- LevelMeter ------------------------------------------------------------------------

void LevelMeter::push (float peak)
{
    const float db = peak > 1e-6f ? 20.f * std::log10 (peak) : -100.f;
    levelDb = std::max (db, levelDb - 1.5f);
    if (db >= holdDb) { holdDb = db; holdTicks = 45; }
    else if (--holdTicks <= 0) holdDb = std::max (-100.f, holdDb - 1.5f);

    // Below the scale nothing shows; otherwise a quarter dB is a pixel or less.
    const float lv = std::max (levelDb, -61.f), hv = std::max (holdDb, -61.f);
    if (std::abs (lv - shownLevel) > 0.25f || std::abs (hv - shownHold) > 0.25f)
    {
        shownLevel = lv;
        shownHold = hv;
        repaint();
    }
}

void LevelMeter::setMissing (bool m, juce::String why)
{
    if (m == missing && why == missingText) return;
    missing = m;
    missingText = std::move (why);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    auto b = getLocalBounds().toFloat();
    auto top = b.removeFromTop (13.f);
    caption (g, name, top, juce::Justification::centredLeft, col::ink, 9.5f);
    g.setFont (mono (9.5f));
    if (missing)
    {
        g.setColour (col::accent);
        g.setFont (label (9.f));
        g.drawText (missingText.toUpperCase(), top, juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (col::inkSoft);
        g.drawText (levelDb <= -60.f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : signedNumber (levelDb, 0).replace ("+", ""),
                    top, juce::Justification::centredRight, false);
    }

    const auto bar = b.removeFromTop (6.f).translated (0.f, 2.f);
    const auto x = [&] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f); };
    g.setColour (col::well);
    g.fillRect (bar);
    if (missing)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (bar.toNearestInt());
        hatch (g, bar, col::accent.withAlpha (0.55f), 3.f, 1.f);
    }
    else
    {
        g.setColour (col::inkSoft);
        g.fillRect (bar.withRight (x (levelDb)));
        if (holdDb > -60.f)
        {
            g.setColour (holdDb > -0.5f ? col::accent : col::ink);
            g.fillRect (juce::Rectangle<float> (x (holdDb) - 1.f, bar.getY() - 1.f, 2.f, bar.getHeight() + 2.f));
        }
    }
    g.setColour (col::hairline);
    g.drawRect (bar, 0.8f);
    g.setColour (col::inkFaint);
    for (float db : { -48.f, -36.f, -24.f, -12.f, 0.f })
        g.fillRect (juce::Rectangle<float> (x (db) - 0.5f, bar.getBottom() + 1.5f, 1.f, 3.f));
}

// ---- TextLink ----------------------------------------------------------------------------

void TextLink::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto b = getLocalBounds().toFloat();
    caption (g, text, b, juce::Justification::centredLeft, isMouseOver() ? col::ink : col::inkSoft, 9.5f);
    g.setFont (label (9.5f));
    const float w = textWidth (g.getCurrentFont(), text.toUpperCase());
    g.fillRect (b.getX(), b.getCentreY() + 6.f, w, 1.f);
}

void TextLink::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}
