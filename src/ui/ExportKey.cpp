#include "ExportKey.h"

using namespace theme;

namespace
{
    using Stage = PhaseFixerProcessor::ExportStage;

    juce::String textFor (const PhaseFixerProcessor::ExportInfo& e)
    {
        switch (e.stage)
        {
            case Stage::idle: return "Rec pass";
            case Stage::armed: return "Waiting";
            case Stage::recording: return juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f Rec ")) + fixed (e.seconds, 1) + " s";
            case Stage::rendering: return "Rendering";
            case Stage::ready: return juce::String (juce::CharPointer_UTF8 ("Drag WAV \xe2\x86\x97"));
            case Stage::failed: return "Failed, retry";
        }
        return {};
    }
} // namespace

ExportKey::ExportKey (PhaseFixerProcessor& p) : proc (p)
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ExportKey::refresh()
{
    const auto was = info.stage;
    info = proc.getExportInfo();
    if (info.stage == Stage::ready && was != Stage::ready)
        readyAt = juce::Time::getMillisecondCounter();
    const auto t = textFor (info);
    if (t != shown)
    {
        shown = t;
        setMouseCursor (info.stage == Stage::ready ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ExportKey::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto r = getLocalBounds().toFloat().reduced (1.f);
    const bool live = info.stage == Stage::recording;
    const bool solid = live || info.stage == Stage::ready;
    const bool loud = live || info.stage == Stage::armed || info.stage == Stage::failed;
    const auto c = loud ? col::accent : col::ink;
    if (solid)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r, kRadius);
    }
    else
    {
        g.setColour (loud || isMouseOver() ? c : col::inkSoft);
        g.drawRoundedRectangle (r, kRadius, loud && isMouseOver() ? 1.6f : 1.f);
    }
    caption (g, shown, r, juce::Justification::centred, solid ? col::sheet : c, 9.f);
}

void ExportKey::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    menu = e.mods.isPopupMenu();
    if (! menu) return;
    const auto file = info.file;
    juce::PopupMenu m;
   #if JUCE_MAC
    const juce::String reveal ("Show in Finder");
   #else
    const juce::String reveal ("Show in Explorer");
   #endif
    m.addItem (reveal, file.existsAsFile(), false, [file] { file.revealToUser(); });
    m.addItem ("Record another pass", info.stage != Stage::rendering, false,
               [safe = juce::Component::SafePointer<ExportKey> (this)] {
                   if (safe == nullptr) return;
                   if (safe->info.stage == Stage::ready || safe->info.stage == Stage::failed || safe->info.stage == Stage::idle)
                       safe->proc.toggleExport();
                   safe->refresh();
               });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void ExportKey::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || info.stage != Stage::ready || ! info.file.existsAsFile() || e.getDistanceFromDragStart() < 4) return;
    dragged = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ info.file.getFullPathName() }, false, this);
}

void ExportKey::mouseUp (const juce::MouseEvent& e)
{
    if (dragged || menu || ! getLocalBounds().contains (e.getPosition())) return;
    proc.toggleExport();
    refresh();
}
