#include "PluginEditor.h"

using namespace theme;

namespace
{
    juce::String hzText (float v)
    {
        return v < 1000.f ? theme::fixed (v, v < 100.f ? 1 : 0) + " Hz" : theme::fixed (v / 1000.f, 2) + " kHz";
    }

    juce::String failureText (pf::AlignStatus s)
    {
        switch (s)
        {
            case pf::AlignStatus::notEnoughSignal: return juce::String (juce::CharPointer_UTF8 ("Not enough shared signal \xe2\x80\x94 play a part where both tracks sound, then ALIGN again."));
            case pf::AlignStatus::noReference: return "The sidechain was silent. Route the reference track into it and play.";
            case pf::AlignStatus::noTrack: return "This track was silent while listening.";
            case pf::AlignStatus::unrelated: return juce::String (juce::CharPointer_UTF8 ("These two don\xe2\x80\x99t share a sound \xe2\x80\x94 check the sidechain source."));
            case pf::AlignStatus::outOfRange: return juce::String (juce::CharPointer_UTF8 ("They\xe2\x80\x99re more than 50 ms apart \xe2\x80\x94 move the track closer in the arrangement first."));
            case pf::AlignStatus::ok: break;
        }
        return {};
    }
} // namespace

PhaseFuckerEditor::PhaseFuckerEditor (PhaseFuckerProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      shiftDial (p.param (ids::shift), "Shift", [] (float v) { return signedNumber (v, 3) + " ms"; }),
      freqDial (p.param (ids::rotateFreq), "Freq", hzText),
      qDial (p.param (ids::rotateQ), "Q", [] (float v) { return juce::String (v, 2); }),
      polarityKey (p.param (ids::invert), juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), 22.f),
      rotateKey (p.param (ids::rotate), "Rotate", 10.5f),
      compareKey (p.param (ids::compare), "A/B", 11.f)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (sheet);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &refMeter, &trackMeter, &alignKey, &scope, &strip, &shiftDial,
                                                                       &freqDial, &qDial, &polarityKey, &rotateKey, &compareKey,
                                                                       &lookahead, &resetLink })
        sheet.addAndMakeVisible (c);

    shiftDial.setShowsValue (false);
    shiftDial.setBipolar (true);
    shiftDial.onValueChange = [this] { sheet.repaint (100, 500, 220, 70); };
    compareKey.setAccentWhenOn (true);
    rotateKey.onChange = [this] {
        freqDial.setDimmed (! rotateKey.isOn());
        qDial.setDimmed (! rotateKey.isOn());
    };
    rotateKey.onChange();

    alignKey.onClick = [this] { proc.align(); tick(); };
    resetLink.onClick = [this] { proc.resetCorrection(); };
    lookahead.onChoose = [this] (int m) { proc.setLookaheadMode (m); tick(); };

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 3 / 2, kHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
    layout();
    tick();
    startTimerHz (30);
}

PhaseFuckerEditor::~PhaseFuckerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PhaseFuckerEditor::resized()
{
    sheet.setBounds (0, 0, kWidth, kHeight);
    sheet.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

void PhaseFuckerEditor::layout()
{
    refMeter.setBounds (392, 22, 130, 26);
    trackMeter.setBounds (540, 22, 130, 26);
    alignKey.setBounds (696, 16, 160, 44);

    scope.setBounds (24, 88, 832, 252);
    strip.setBounds (24, 354, 832, 112);

    shiftDial.setBounds (24, 502, 78, 86);
    resetLink.setBounds (114, 566, 60, 14);
    polarityKey.setBounds (336, 522, 46, 46);
    rotateKey.setBounds (420, 522, 78, 28);
    freqDial.setBounds (510, 500, 76, 90);
    qDial.setBounds (590, 500, 64, 90);
    compareKey.setBounds (686, 522, 54, 30);
    lookahead.setBounds (758, 504, 98, 34);
}

void PhaseFuckerEditor::Sheet::paint (juce::Graphics& g)
{
    auto& ed = editor;
    g.fillAll (col::paper);
    cropMarks (g, getLocalBounds().toFloat().reduced (8.f), 7.f);

    // Wordmark.
    g.setFont (title (27.f));
    const juce::String a ("PHASE"), slash ("/"), b ("FUCKER");
    float x = 24.f;
    for (auto [s, c] : { std::pair { a, col::ink }, { slash, col::accent }, { b, col::ink } })
    {
        g.setColour (c);
        const float w = textWidth (g.getCurrentFont(), s);
        g.drawText (s, juce::Rectangle<float> (x, 14.f, w + 2.f, 30.f), juce::Justification::centredLeft, false);
        x += w + (s == slash ? 1.f : 0.f);
    }
    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("AUTOMATIC PHASE ALIGNMENT \xc2\xb7 PF-1 \xc2\xb7 v")) + JucePlugin_VersionString,
                juce::Rectangle<float> (24.f, 46.f, 330.f, 14.f), juce::Justification::centredLeft, false);

    g.setColour (col::ink);
    g.fillRect (24.f, 76.f, 832.f, 1.5f);

    // Section C.
    g.setColour (col::hairline);
    g.fillRect (24.f, 478.f, 832.f, 1.f);
    sectionTag (g, "C", "Correction", { 24.f, 484.f });
    for (float dx : { 318.f, 402.f, 668.f })
        g.fillRect (dx, 504.f, 1.f, 80.f);

    // Shift readout.
    {
        const double ms = ed.proc.correction().shiftMs;
        const auto value = signedNumber (ms, 3);
        g.setFont (monoStrong (30.f));
        g.setColour (ed.proc.isComparing() ? col::inkFaint : col::ink);
        const float w = textWidth (g.getCurrentFont(), value);
        g.drawText (value, juce::Rectangle<float> (110.f, 506.f, w + 4.f, 36.f), juce::Justification::centredLeft, false);
        g.setFont (mono (12.f));
        g.drawText ("ms", juce::Rectangle<float> (114.f + w, 518.f, 30.f, 20.f), juce::Justification::centredLeft, false);
        const double fs = ed.proc.getRate();
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        const auto rate = fs >= 1000.0 ? fixed (fs / 1000.0, std::fmod (fs, 1000.0) == 0.0 ? 0 : 1) + " kHz" : juce::String (fs);
        g.drawText (signedNumber (ms * 0.001 * fs, 1) + " smp @ " + rate + (ms < 0.0 ? "  earlier" : ms > 0.0 ? "  later" : ""),
                    juce::Rectangle<float> (112.f, 544.f, 200.f, 14.f), juce::Justification::centredLeft, false);
    }
    caption (g, "Polarity", { 336.f, 502.f, 70.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    caption (g, "Allpass", { 420.f, 502.f, 80.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    caption (g, "Compare", { 686.f, 502.f, 70.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    g.setFont (mono (9.f));
    g.setColour (col::inkFaint);
    g.drawText ("phase turn", juce::Rectangle<float> (420.f, 554.f, 90.f, 12.f), juce::Justification::centredLeft, false);
    g.drawText ("uncorrected", juce::Rectangle<float> (686.f, 556.f, 70.f, 12.f), juce::Justification::centredLeft, false);

    // Footer.
    g.setColour (col::ink);
    g.fillRect (24.f, 594.f, 832.f, 1.f);
    const auto foot = juce::Rectangle<float> (24.f, 597.f, 832.f, 18.f);
    g.setFont (mono (10.5f));
    g.setColour (ed.statusWarn ? col::accent : col::ink);
    g.drawText (ed.status, foot.withTrimmedRight (170.f), juce::Justification::centredLeft, true);
    g.setColour (col::inkSoft);
    g.drawText (ed.statusRight, foot, juce::Justification::centredRight, false);
}

void PhaseFuckerEditor::tick()
{
    ++ticks;
    const auto st = proc.getAlignState();
    const auto now = juce::Time::getMillisecondCounter();

    // A fresh result: jump the scope to the hit it was measured on.
    if (st.hasResult && st.finishedAt != seenResult)
    {
        seenResult = st.finishedAt;
        if (st.finishedAt != 0 && st.hitSample >= 0)
            scope.showHit (st.hitSample, 4000);
    }

    // Meters.
    const float rp = proc.takePeak (true), tp = proc.takePeak (false);
    refMeter.push (rp);
    trackMeter.push (tp);
    refQuietTicks = (rp < 1e-4f && tp > 1e-3f) ? refQuietTicks + 1 : 0;
    if (! proc.hasSidechainBus()) refMeter.setMissing (true, "no sidechain");
    else if (refQuietTicks > 45) refMeter.setMissing (true, "silent");
    else refMeter.setMissing (false);

    // ALIGN key.
    using Look = AlignKey::Look;
    const bool fresh = st.hasResult && st.finishedAt != 0 && now - st.finishedAt < 1600;
    switch (st.stage)
    {
        case PhaseFuckerProcessor::Stage::listening: alignKey.setLook (Look::listening, st.progress); break;
        case PhaseFuckerProcessor::Stage::analysing: alignKey.setLook (Look::analysing, 1.f); break;
        case PhaseFuckerProcessor::Stage::idle:
        case PhaseFuckerProcessor::Stage::done:
            alignKey.setLook (fresh ? (st.result.status == pf::AlignStatus::ok ? Look::aligned : Look::failed) : Look::idle, 0.f);
    }

    lookahead.set (proc.getLookaheadMode(), proc.getLookaheadMs(), proc.isShiftClamped());

    scope.refresh();
    if (ticks % 2 == 0) strip.refresh();
    updateStatus();
    sheet.repaint (0, 500, kWidth, kHeight - 500);
}

void PhaseFuckerEditor::updateStatus()
{
    const auto st = proc.getAlignState();
    const auto c = proc.correction();
    juce::String s, right;
    bool warn = false;
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    if (st.stage == PhaseFuckerProcessor::Stage::listening)
    {
        s = juce::String (juce::CharPointer_UTF8 ("Listening \xe2\x80\x94 play a part where both tracks sound. Press again to use what\xe2\x80\x99s there."));
        right = juce::String (st.progress * PhaseFuckerProcessor::kNeededSeconds, 1) + " / " + juce::String (PhaseFuckerProcessor::kNeededSeconds, 1) + " s";
    }
    else if (st.stage == PhaseFuckerProcessor::Stage::analysing)
        s = juce::String (juce::CharPointer_UTF8 ("Analysing the last 10 seconds\xe2\x80\xa6"));
    else if (! proc.hasSidechainBus())
    {
        s = juce::String (juce::CharPointer_UTF8 ("No reference \xe2\x80\x94 route the track to match into the sidechain (Logic: Side Chain menu above \xc2\xb7 Live: Sidechain in the device)."));
        warn = true;
    }
    else if (refQuietTicks > 45)
    {
        s = "The sidechain is silent while this track plays. Is the reference routed and playing?";
        warn = true;
    }
    else if (proc.isShiftClamped())
    {
        s = juce::String (juce::CharPointer_UTF8 ("This shift needs more lookahead \xe2\x80\x94 set LOOKAHEAD to Auto or at least ")) + juce::String ((int) std::ceil (-c.shiftMs)) + " ms.";
        warn = true;
    }
    else if (proc.isComparing())
        s = juce::String (juce::CharPointer_UTF8 ("A/B: the track plays uncorrected (same latency) \xe2\x80\x94 switch off to hear the alignment."));
    else if (st.hasResult && st.result.status == pf::AlignStatus::ok)
    {
        const auto& r = st.result;
        const auto& rc = r.correction;
        const bool edited = std::abs (c.shiftMs - rc.shiftMs) > 5e-4 || c.invert != rc.invert || c.rotate != rc.rotate
                         || (c.rotate && (std::abs (c.rotateHz - rc.rotateHz) > 0.5 || std::abs (c.rotateQ - rc.rotateQ) > 0.01));
        s = juce::String (edited ? "Detected " : "Aligned ") + signedNumber (rc.shiftMs, 3) + " ms";
        s += dot + (rc.invert ? "polarity flipped" : "polarity kept");
        if (rc.rotate) s += dot + "allpass " + hzText ((float) rc.rotateHz) + " Q " + juce::String (rc.rotateQ, 2);
        s += dot + "low end " + signedNumber (r.lowEndGainDb, 1) + " dB";
        if (edited) s += dot + "adjusted by hand";
        right = "CONFIDENCE " + juce::String (juce::roundToInt (r.confidence * 100.f)) + " %";
        warn = r.confidence < 0.5f;
        if (warn) right = "LOW CONFIDENCE " + juce::String (juce::roundToInt (r.confidence * 100.f)) + " %";
    }
    else if (st.hasResult)
    {
        s = failureText (st.result.status);
        warn = true;
    }
    else
        s = juce::String (juce::CharPointer_UTF8 ("Press ALIGN while both tracks play \xe2\x80\x94 it listens until it has heard about 2 s of them together."));

    if (s != status || right != statusRight || warn != statusWarn)
    {
        status = s;
        statusRight = right;
        statusWarn = warn;
    }
}
