#include "PluginEditor.h"

using namespace theme;

namespace
{
    const juce::String kDeg (juce::CharPointer_UTF8 ("\xc2\xb0"));
    const juce::String kDot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    juce::String degrees (double v) { return signedNumber (v, 1) + kDeg; }

    juce::String failureText (pf::AlignStatus s)
    {
        switch (s)
        {
            case pf::AlignStatus::notEnoughSignal: return juce::String (juce::CharPointer_UTF8 ("Not enough shared signal \xe2\x80\x94 play a part where both tracks sound, then ALIGN again."));
            case pf::AlignStatus::noReference: return "The sidechain was silent. Route the reference track into it and play.";
            case pf::AlignStatus::noTrack: return "This track was silent while listening.";
            case pf::AlignStatus::unrelated: return juce::String (juce::CharPointer_UTF8 ("These two don\xe2\x80\x99t share a sound \xe2\x80\x94 check the sidechain source."));
            case pf::AlignStatus::ok: break;
        }
        return {};
    }
} // namespace

PhaseFuckerEditor::PhaseFuckerEditor (PhaseFuckerProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      modeSwitch (p.param (ids::perHit), { "One turn", "Per hit" }),
      phaseDial (p.param (ids::phase), "Phase", [] (float v) { return degrees (v); }),
      polarityKey (p.param (ids::invert), juce::String (juce::CharPointer_UTF8 ("\xc3\x98")), 22.f),
      compareKey (p.param (ids::compare), "A/B", 11.f)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (sheet);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &refMeter, &trackMeter, &alignKey, &scope, &strip, &lane,
                                                                       &modeSwitch, &exportKey, &phaseDial, &polarityKey, &compareKey, &resetLink })
        sheet.addAndMakeVisible (c);
    modeSwitch.onChange = [this] { tick(); };
    lane.setVisible (false);

    phaseDial.setShowsValue (false);
    phaseDial.setBipolar (true);
    phaseDial.onValueChange = [this] { sheet.repaint (readoutArea()); };
    polarityKey.onChange = [this] { sheet.repaint (readoutArea()); };
    compareKey.setAccentWhenOn (true);
    compareKey.onChange = [this] { sheet.repaint (readoutArea()); };

    alignKey.onClick = [this] { proc.align(); tick(); };
    resetLink.onClick = [this] { proc.resetCorrection(); };

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
    lane.setBounds (24, 354, 832, 112);

    modeSwitch.setBounds (150, 482, 140, 17);
    exportKey.setBounds (764, 482, 92, 17);

    phaseDial.setBounds (24, 502, 78, 86);
    resetLink.setBounds (114, 568, 60, 14);
    polarityKey.setBounds (336, 522, 46, 46);
    compareKey.setBounds (420, 522, 54, 30);
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
    g.drawText (juce::String (juce::CharPointer_UTF8 ("LINEAR-PHASE ALIGNMENT \xc2\xb7 PF-1 \xc2\xb7 v")) + JucePlugin_VersionString,
                juce::Rectangle<float> (24.f, 46.f, 330.f, 14.f), juce::Justification::centredLeft, false);

    g.setColour (col::ink);
    g.fillRect (24.f, 76.f, 832.f, 1.5f);

    // Section C.
    g.setColour (col::hairline);
    g.fillRect (24.f, 478.f, 832.f, 1.f);
    sectionTag (g, "C", "Correction", { 24.f, 484.f });
    caption (g, "Turn", { 122.f, 483.f, 28.f, 15.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
    caption (g, "Export", { 700.f, 483.f, 60.f, 15.f }, juce::Justification::centredRight, col::inkSoft, 9.f);
    for (float dx : { 318.f, 402.f, 496.f })
        g.fillRect (dx, 504.f, 1.f, 80.f);

    // Phase readout. Per hit: the turn playing now (map plus offset).
    {
        const auto c = ed.proc.correction();
        const bool off = ed.proc.isComparing();
        g.setFont (monoStrong (30.f));
        g.setColour (off ? col::inkFaint : col::ink);
        juce::String big, line;
        if (ed.perHitShown)
        {
            big = degrees (ed.liveShown);
            line = off ? juce::String ("A/B: correction off")
                 : ed.mapPoints == 0 ? juce::String ("no map yet, ALIGN learns one")
                 : std::abs (c.totalDeg()) > 0.05 ? "map " + juce::String (c.totalDeg() > 0.0 ? "+ " : juce::String (juce::CharPointer_UTF8 ("â ")))
                                                     + degrees (std::abs (c.totalDeg())).substring (1) + " offset"
                                                   : juce::String ("now, from the map");
        }
        else
        {
            big = degrees (c.phaseDeg);
            line = off ? juce::String ("A/B: correction off")
                 : c.invert ? "flipped, " + degrees (c.totalDeg()) + " in all"
                            : juce::String ("every frequency, linear phase");
        }
        g.drawText (big, juce::Rectangle<float> (110.f, 506.f, 200.f, 36.f), juce::Justification::centredLeft, false);
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        g.drawText (line, juce::Rectangle<float> (112.f, 544.f, 200.f, 14.f), juce::Justification::centredLeft, false);
    }
    caption (g, "Polarity", { 336.f, 502.f, 70.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    caption (g, "Compare", { 420.f, 502.f, 70.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    g.setFont (mono (9.f));
    g.setColour (col::inkFaint);
    g.drawText ("uncorrected", juce::Rectangle<float> (420.f, 556.f, 70.f, 12.f), juce::Justification::centredLeft, false);

    // Time anchor: how far apart the two arrive, which a rotation can't move.
    {
        const auto area = ed.anchorArea().toFloat().withTrimmedLeft (4.f);
        caption (g, "Time anchor", area.withHeight (14.f).translated (0.f, 4.f), juce::Justification::centredLeft, col::inkSoft, 9.5f);
        const auto st = ed.proc.getAlignState();
        const double fs = ed.proc.getRate();
        juce::String line1 = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), line2 = "ALIGN measures how far apart the two arrive.";
        bool warn = false;
        if (st.hasResult && st.result.status == pf::AlignStatus::ok)
        {
            const auto& r = st.result;
            if (! r.hasOffset && r.coherence < 0.2f)
            {
                // A kick and a bass: no common arrival to measure.
                line1 = "different sounds";
                line2 = "nothing to line up in time, only the turn";
            }
            else if (r.hasOffset)
            {
                const double ms = std::abs (r.offsetSamples) / fs * 1000.0;
                line1 = "track " + theme::fixed (ms, 3) + " ms " + (r.offsetSamples > 0.0 ? "late" : "early");
                if (r.timeFixDb >= 0.5f)
                {
                    line2 = "moving it " + juce::String (r.offsetSamples > 0.0 ? "earlier" : "later") + " in the arrangement adds "
                          + signedNumber (r.timeFixDb, 1) + " dB";
                    warn = true;
                }
                else
                    line2 = "the rotation covers it";
            }
            else
            {
                line1 = "in time";
                line2 = "nothing to move, the rotation is the whole fix";
            }
        }
        g.setFont (mono (13.f));
        g.setColour (warn ? col::accent : col::ink);
        g.drawText (line1, area.withTop (area.getY() + 22.f).withHeight (18.f), juce::Justification::centredLeft, false);
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        g.drawText (line2, area.withTop (area.getY() + 44.f).withHeight (14.f), juce::Justification::centredLeft, true);
        g.setColour (col::inkFaint);
        g.drawText ("latency " + theme::fixed (ed.proc.getLatencyMs(), 1) + " ms, compensated by the host",
                    area.withTop (area.getY() + 66.f).withHeight (14.f), juce::Justification::centredLeft, false);
    }

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

    // One turn or per hit: section B and the readout follow. When one turn
    // was found but the hits pull apart, PER HIT is outlined.
    const bool perHit = proc.isPerHit();
    modeSwitch.setHint (! perHit && st.hasResult && ! st.perHit && st.result.status == pf::AlignStatus::ok
                                && st.result.hits.size() >= 4 && st.result.hitAgreement < 0.6f ? 1 : -1);
    if (perHit != perHitShown)
    {
        perHitShown = perHit;
        strip.setVisible (! perHit);
        lane.setVisible (perHit);
        phaseDial.setLabel (perHit ? "Offset" : "Phase");
        sheet.repaint (readoutArea());
    }
    if (perHit)
    {
        lane.refresh();
        const int points = proc.getMap().size();
        const double live = proc.getLiveTurn();
        if (points != mapPoints || std::abs (live - liveShown) > 0.05)
        {
            mapPoints = points;
            liveShown = live;
            sheet.repaint (readoutArea());
        }
    }
    else if (ticks % 2 == 0)
        strip.refresh();

    scope.refresh();
    exportKey.refresh();
    updateStatus();
}

void PhaseFuckerEditor::updateStatus()
{
    const auto st = proc.getAlignState();
    const auto c = proc.correction();
    juce::String s, right;
    bool warn = false;

    using ES = PhaseFuckerProcessor::ExportStage;
    const auto ex = proc.getExportInfo();
    const bool exportFresh = ex.stage == ES::ready && juce::Time::getMillisecondCounter() - exportKey.readySince() < 20000;

    if (st.stage == PhaseFuckerProcessor::Stage::listening)
    {
        s = juce::String (juce::CharPointer_UTF8 ("Listening \xe2\x80\x94 play a part where both tracks sound. Press again to use what\xe2\x80\x99s there."));
        right = theme::fixed (st.progress * PhaseFuckerProcessor::kNeededSeconds, 1) + " / " + theme::fixed (PhaseFuckerProcessor::kNeededSeconds, 1) + " s";
    }
    else if (st.stage == PhaseFuckerProcessor::Stage::analysing)
        s = juce::String (juce::CharPointer_UTF8 ("Analysing the last 10 seconds\xe2\x80\xa6"));
    else if (ex.stage == ES::armed)
        s = "Export: recording starts when playback starts or the loop comes round. Click again to cancel.";
    else if (ex.stage == ES::recording)
    {
        s = "Recording the corrected track from " + ex.where + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 it stops when the loop comes round or playback stops. Click to stop now."));
        right = "REC " + theme::fixed (ex.seconds, 1) + " s";
    }
    else if (ex.stage == ES::rendering)
        s = juce::String (juce::CharPointer_UTF8 ("Rendering the pass\xe2\x80\xa6"));
    else if (ex.stage == ES::failed && exportFresh)
    {
        s = "Export failed: nothing recorded, or the file couldn't be written. Click to try again.";
        warn = true;
    }
    else if (exportFresh)
    {
        s = "Saved " + theme::fixed (ex.seconds, 2) + " s from " + ex.where + " to Music/PhaseFucker. Drag DRAG WAV into the arrangement at " + ex.where + ".";
        right = ex.file.getFileName().length() > 26 ? juce::String() : ex.file.getFileName();
    }
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
    else if (proc.isComparing())
        s = juce::String (juce::CharPointer_UTF8 ("A/B: the track plays uncorrected (same latency) \xe2\x80\x94 switch off to hear the alignment."));
    else if (st.hasResult && st.perHit && (st.noTimeline || (st.result.status == pf::AlignStatus::ok && st.mappedHits == 0)))
    {
        s = st.noTimeline ? juce::String (juce::CharPointer_UTF8 ("Per hit needs the DAW playing \xe2\x80\x94 loop the part, start playback, then ALIGN."))
                          : juce::String (juce::CharPointer_UTF8 ("No hits in the reference to map \xe2\x80\x94 the kick (or what leads) goes on the sidechain."));
        warn = true;
    }
    else if (st.hasResult && st.perHit && st.result.status == pf::AlignStatus::ok)
    {
        const auto map = proc.getMap();
        double gain = 0.0;
        for (const auto& p : map.points) gain += p.gainDb;
        s = "Mapped " + juce::String (st.mappedHits) + (st.mappedHits == 1 ? " hit" : " hits");
        if (map.size() != st.mappedHits) s += " (" + juce::String (map.size()) + " in the map)";
        if (! map.empty()) s += kDot + "low end " + signedNumber (gain / map.size(), 1) + " dB per hit";
        if (std::abs (c.totalDeg()) > 0.05) s += kDot + "offset " + degrees (c.totalDeg());
        const auto pct = juce::String (juce::roundToInt (st.result.hitConfidence * 100.f)) + " %";
        warn = st.result.hitConfidence < 0.5f;
        right = (warn ? "LOW CONFIDENCE " : "CONFIDENCE ") + pct;
    }
    else if (st.hasResult && st.result.status == pf::AlignStatus::ok)
    {
        const auto& r = st.result;
        const auto& rc = r.correction;
        const bool edited = std::abs (c.phaseDeg - rc.phaseDeg) > 0.05 || c.invert != rc.invert;
        s = juce::String (edited ? "Detected " : "Turned ") + degrees (rc.phaseDeg);
        if (rc.invert) s += " + flip";
        if (r.coherence < 0.2f && ! r.hits.empty())   // a kick and a bass: only where they meet counts
            s += kDot + "low end " + signedNumber (r.lowEndGainDb, 1) + " dB where they meet";
        else
            s += kDot + "low end " + signedNumber (r.lowEndGainDb, 1) + " dB" + kDot + "full band " + signedNumber (r.sumGainDb, 1) + " dB";
        if (edited) s += kDot + "adjusted by hand";
        // The hits pull apart (a bass changing notes under the kick): one turn is a compromise.
        else if (r.hits.size() >= 4 && r.hitAgreement < 0.6f) s += kDot + "the hits want different turns, try PER HIT";
        right = "CONFIDENCE " + juce::String (juce::roundToInt (r.confidence * 100.f)) + " %";
        warn = r.confidence < 0.5f;
        if (warn) right = "LOW CONFIDENCE " + juce::String (juce::roundToInt (r.confidence * 100.f)) + " %";
    }
    else if (st.hasResult)
    {
        s = failureText (st.result.status);
        warn = true;
    }
    else if (proc.isPerHit())
        s = juce::String (juce::CharPointer_UTF8 ("Per hit: loop the part, press play, then ALIGN \xe2\x80\x94 every hit of the reference gets its own turn."));
    else
        s = juce::String (juce::CharPointer_UTF8 ("Press ALIGN while both tracks play \xe2\x80\x94 it listens until it has heard about 2 s of them together."));

    if (s != status || right != statusRight || warn != statusWarn)
    {
        status = s;
        statusRight = right;
        statusWarn = warn;
        sheet.repaint (footerArea());
    }

    // The time-anchor block follows the last result and the sample rate.
    const auto key = juce::String (st.finishedAt) + "/" + juce::String ((int) st.result.status) + "/" + juce::String (proc.getRate());
    if (key != anchorKey)
    {
        anchorKey = key;
        sheet.repaint (anchorArea());
    }
}
