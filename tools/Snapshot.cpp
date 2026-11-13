// Renders the editor offscreen to PNGs, so the UI can be reviewed without a DAW.
//   phasefixer_snapshot <out-dir> [state ...]
// The material is the demo host's kick and rolling bass (the PF_DEMO_KIT
// samples if set, else PsyLoop.h), in the demo's scenarios.
// States (default: all but bench and roundtrip):
//   empty      fresh instance, nothing played
//   idle       the loop and a copy through a second chain (darker, turned
//              115 degrees) played through, not aligned yet
//   listening  ALIGN pressed with too little signal heard
//   aligned    the same, aligned
//   offset     the kick's sub layer, 2.9 ms later and flipped, aligned
//   compare    aligned, A/B on
//   nosc       sidechain bus disabled
//   suggest    kick on the sidechain, the free-running bass: ONE TURN, aligned
//              (the footer suggests PER HIT)
//   perhit     the same, PER HIT, aligned, then playing on into bar 2 (map
//              lane, live turn)
//   mapempty   PER HIT, nothing learned yet
//   longmap    PER HIT, a map over 96 bars loaded with a session (ALIGN in
//              several parts of a song): the lane's grid and bar numbers thin out
//   export     aligned, one pass recorded and rendered (into <out-dir>)
//   roundtrip  per hit, aligned, one point edited; saves the session into a
//              fresh instance and checks the map came back (no image)
//   bench      aligned, then times scope paints at three zooms, the whole
//              editor, and UI ticks, on a native (CoreGraphics) image at 2x
//   typing     aligned, the phase readout double-clicked: the text field over it
//   panned     aligned, zoomed in to 120 ms and scrolled sideways into the bar
//   gestures   aligned, then drives the knob and scope gestures through their
//              handlers and prints PASS/FAIL per check (no image; exit 1 on a FAIL)

#include "DemoKit.h"
#include "PsyLoop.h"
#include "TempoPlayHead.h"
#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
    // The loop is 145 BPM from sample 0; the processor gets a matching transport.
    TempoPlayHead playHead;

    // PFSNAP_SPLIT=0: a host that runs its blocks straight through the loop's
    // end, each block reporting only where it starts (as some DAWs do),
    // instead of ending a block there. hostClock: samples played so far.
    const bool splitAtLoop = [] { const auto* s = std::getenv ("PFSNAP_SPLIT"); return s == nullptr || juce::String (s).getIntValue() != 0; }();
    juce::int64 hostClock = 0;

    void feed (PhaseFixerProcessor& p, const testutil::Signal& ref, const testutil::Signal& trk, size_t from, size_t to)
    {
        p.setPlayHead (&playHead);
        playHead.sampleRate = p.getRate();
        playHead.playing = true;
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        if (! splitAtLoop)   // whole blocks only, on from where the last call ended, round the loop
        {
            const auto loop = (juce::int64) ref.size();
            for (const auto end = hostClock + (juce::int64) (to - from); hostClock < end; hostClock += 512)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto at = (size_t) ((hostClock + i) % loop);
                    for (int c = 0; c < 2; ++c)
                    {
                        buf.setSample (c, i, trk[at]);
                        buf.setSample (c + 2, i, ref[at]);
                    }
                }
                playHead.position = hostClock % loop;
                p.processBlock (buf, midi);
            }
            return;
        }
        for (size_t s = from; s < to; s += 512)
        {
            const int n = (int) std::min<size_t> (512, to - s);
            buf.setSize (4, n, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                buf.copyFrom (c, 0, trk.data() + s, n);
                buf.copyFrom (c + 2, 0, ref.data() + s, n);
            }
            playHead.position = (juce::int64) s;
            p.processBlock (buf, midi);
        }
    }

    // One block with the transport stopped, as hosts keep processing.
    void stop (PhaseFixerProcessor& p)
    {
        playHead.playing = false;
        juce::AudioBuffer<float> buf (4, 512);
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        playHead.playing = true;
    }

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    void waitForResult (PhaseFixerProcessor& p)
    {
        for (int i = 0; i < 200; ++i)
        {
            pump (25);
            if (p.getAlignState().stage == PhaseFixerProcessor::Stage::done) return;
        }
        std::printf ("timed out waiting for the analysis\n");
    }

    // Milliseconds per paint of `c` into an image at 2x: native (CoreGraphics)
    // or JUCE's own software renderer.
    double timePaint (juce::Component& c, int runs, bool native = true)
    {
        juce::Image img (juce::Image::ARGB, c.getWidth() * 2, c.getHeight() * 2, true,
                         native ? (const juce::ImageType&) juce::NativeImageType() : (const juce::ImageType&) juce::SoftwareImageType());
        const auto once = [&] {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (2.f));
            c.paintEntireComponent (g, false);
        };
        once();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < runs; ++i) once();
        return (juce::Time::getMillisecondCounterHiRes() - t0) / runs;
    }

    // A mouse event at `pos` on `c` (its own coordinates).
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), pos, {}, juce::MouseInputSource::defaultPressure,
                 juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                 juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                 &c, &c, now, pos, now, clicks, false };
    }

    // A double-click as JUCE delivers it: down, up, down, up, then the
    // double-click (Component::internalMouseUp sends it after the second up).
    void doubleClick (juce::Component& c, juce::Point<float> pos)
    {
        c.mouseDown (mouseAt (c, pos));
        c.mouseUp (mouseAt (c, pos));
        c.mouseDown (mouseAt (c, pos, 2));
        c.mouseUp (mouseAt (c, pos, 2));
        c.mouseDoubleClick (mouseAt (c, pos, 2));
    }

    // How deep a parameter's change gestures nest: a reset written inside an
    // open drag gesture would reach 2.
    struct GestureDepth : juce::AudioProcessorParameter::Listener
    {
        int depth = 0, deepest = 0;
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            depth += starting ? 1 : -1;
            deepest = std::max (deepest, depth);
        }
    };

    // A trackpad scroll (natural direction off).
    void scroll (juce::Component& c, juce::Point<float> pos, float dx, float dy)
    {
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = true;
        w.isInertial = false;
        c.mouseWheelMove (mouseAt (c, pos), w);
    }

    juce::TextEditor* entryIn (juce::Component& c)
    {
        for (auto* child : c.getChildren())
            if (auto* t = dynamic_cast<juce::TextEditor*> (child)) return t;
        return nullptr;
    }

    // The knob and scope gestures, through their real handlers.
    bool checkGestures (PhaseFixerEditor& editor, PhaseFixerProcessor& proc)
    {
        auto& sheet = *editor.getChildComponent (0);
        auto* dial = dynamic_cast<Dial*> (sheet.getComponentAt (63, 530));
        if (dial == nullptr) { std::printf ("FAIL no phase dial at (63, 530)\n"); return false; }
        auto& phase = proc.param (ids::phase);
        const auto deg = [&] { return phase.convertFrom0to1 (phase.getValue()); };
        const auto set = [&] (float d) { phase.setValueNotifyingHost (phase.convertTo0to1 (d)); pump (20); };
        bool ok = true;
        const auto report = [&] (const char* what, bool pass) { std::printf ("%s %s\n", pass ? "PASS" : "FAIL", what); ok = ok && pass; };

        set (40.f);
        doubleClick (*dial, { 39.f, 30.f });   // the disc
        report ("double-click on the knob resets it to 0", std::abs (deg()) < 0.05f && ! dial->isTyping());

        set (40.f);
        doubleClick (sheet, { 130.f, 524.f });   // the readout's number
        auto* entry = entryIn (sheet);
        report ("double-click on the readout number opens the field, value kept", dial->isTyping() && entry != nullptr && std::abs (deg() - 40.f) < 0.05f);
        if (entry != nullptr)
        {
            entry->setText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "33.5", false);
            entry->onReturnKey();
            pump (50);
        }
        report ("typing -33.5 sets it and closes the field", std::abs (deg() + 33.5f) < 0.05f && ! dial->isTyping() && entryIn (sheet) == nullptr);

        // The phase dial shows no number of its own: a double-click on its name resets it too.
        set (40.f);
        doubleClick (*dial, { 39.f, 80.f });
        report ("double-click under a knob without a number resets it", std::abs (deg()) < 0.05f && ! dial->isTyping());

        // A double-click arriving while the drag from its second click is still open.
        {
            GestureDepth gestures;
            phase.addListener (&gestures);
            set (40.f);
            dial->mouseDown (mouseAt (*dial, { 39.f, 30.f }));
            dial->mouseDoubleClick (mouseAt (*dial, { 39.f, 30.f }, 2));
            dial->mouseUp (mouseAt (*dial, { 39.f, 30.f }, 2));
            phase.removeListener (&gestures);
            report ("a double-click mid-drag ends the drag before resetting", std::abs (deg()) < 0.05f && gestures.depth == 0 && gestures.deepest == 1);
        }

        // A knob that shows its number (as the phase dial, 78 x 86, but with its number on 71..86).
        {
            Dial shown (phase, "Phase", [] (float v) { return theme::signedNumber (v, 1) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); });
            shown.setSize (78, 86);
            set (40.f);
            doubleClick (shown, { 39.f, 30.f });
            report ("double-click on a knob with a number resets it", std::abs (deg()) < 0.05f && ! shown.isTyping());
            set (40.f);
            doubleClick (shown, { 6.f, 78.f });   // the value line, left of "+40.0°"
            report ("double-click beside its number resets it", std::abs (deg()) < 0.05f && ! shown.isTyping());
            set (40.f);
            doubleClick (shown, { 39.f, 78.f });
            report ("double-click on its number opens the field, value kept", shown.isTyping() && entryIn (shown) != nullptr && std::abs (deg() - 40.f) < 0.05f);
        }
        set (-33.5f);

        doubleClick (sheet, { 290.f, 524.f });   // beside the number
        report ("double-click beside the number does nothing", ! dial->isTyping() && std::abs (deg() + 33.5f) < 0.05f);

        // Scope: 1/2 bar at the song's tempo, then zoomed in to 100 ms.
        auto& scope = editor.getScope();
        const auto t = proc.getTransport();
        const float bar = (float) (0.5 * t.numerator * 4.0 / t.denominator * 60000.0 / t.bpm);
        scope.setBars (0.5f);
        const float full = scope.windowStart();
        scope.setZoom (100.f);
        const float s0 = scope.windowStart();
        scroll (scope, { 400.f, 120.f }, -0.2f, 0.f);   // fingers to the left: on in time
        report ("scrolling sideways moves the window on", scope.windowStart() > s0 + 1.f);
        scroll (scope, { 400.f, 120.f }, 0.1f, 0.f);
        report ("and back", scope.windowStart() < s0 + 100.f * 0.2f * 400.f / 798.f - 1.f);
        for (int i = 0; i < 60; ++i) scroll (scope, { 400.f, 120.f }, -0.5f, 0.f);
        report ("it stops at the end of the bar length", std::abs (scope.windowStart() + 100.f - (full + bar)) < 0.5f);
        for (int i = 0; i < 60; ++i) scroll (scope, { 400.f, 120.f }, 0.5f, 0.f);
        report ("and at its start", std::abs (scope.windowStart() - full) < 0.5f);
        scroll (scope, { 400.f, 120.f }, -0.3f, 0.f);

        const float f = (434.f - 34.f) / 798.f;   // the plot starts 34 px in, 798 px wide
        const float before = scope.windowStart() + f * proc.scopeZoomMs;
        scroll (scope, { 434.f, 120.f }, 0.f, 0.3f);
        report ("scrolling up zooms in around the pointer", proc.scopeZoomMs < 99.f && std::abs (scope.windowStart() + f * proc.scopeZoomMs - before) < 0.05f);
        const float z = proc.scopeZoomMs;
        scope.mouseMagnify (mouseAt (scope, { 434.f, 120.f }), 2.f);
        report ("a pinch zooms too, around the pointer", std::abs (proc.scopeZoomMs - 0.5f * z) < 0.05f
                                                       && std::abs (scope.windowStart() + f * proc.scopeZoomMs - before) < 0.05f);
        for (int i = 0; i < 40; ++i) scroll (scope, { 434.f, 120.f }, 0.f, -0.5f);
        report ("zoomed out past the bar length, it sits at its start", proc.scopeZoomMs > bar && std::abs (scope.windowStart() + 0.04f * proc.scopeZoomMs) < 0.05f);

        scope.setZoom (100.f);
        scroll (scope, { 400.f, 120.f }, -0.3f, 0.f);
        doubleClick (scope, { 400.f, 120.f });
        report ("double-click: back to the bar length, from its start", juce::exactlyEqual (proc.scopeBars, 0.5f) && std::abs (scope.windowStart() - full) < 0.05f);
        return ok;
    }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    juce::StringArray states;
    for (int i = 2; i < argc; ++i) states.add (argv[i]);
    if (states.isEmpty()) states = { "empty", "idle", "listening", "aligned", "offset", "compare", "nosc", "suggest", "perhit", "mapempty", "longmap", "export" };

    const double fs = 48000.0;
    const auto demoKit = kit::load();
    const auto psyLoop = demoKit ? kit::make (*demoKit, fs, true) : psy::make (fs, true);
    const double bpm = demoKit ? demoKit->bpm : psy::kBpm;
    std::printf ("material: %s\n", demoKit ? demoKit->file.getFullPathName().toRawUTF8() : "synthesized");
    // As the demo plays them: peaks at -3 dBFS.
    const auto atPeak = [] (testutil::Signal x) { float p = 1e-6f; for (float v : x) p = std::max (p, std::abs (v)); return testutil::scaled (x, 0.71 / p); };
    const auto kick = atPeak (psyLoop.kick);
    const auto bass = atPeak (psyLoop.bass);
    const auto lp = [&] (double hz, const testutil::Signal& x) { return testutil::Biquad::lowpass (hz, 0.7, fs).zeroPhase (x); };
    const auto src = testutil::sum (kick, bass);

    bool failed = false;
    for (const auto& state : states)
    {
        hostClock = 0;
        PhaseFixerProcessor proc;
        if (state == "nosc")
        {
            auto layout = proc.getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
            proc.setBusesLayout (layout);
        }
        proc.prepareToPlay (fs, 512);
        proc.exportDirectory = outDir;
        playHead.bpm = bpm;
        playHead.loopQuarters = (double) src.size() / fs * bpm / 60.0;   // the loop points match the audio loop, as in a DAW

        const auto ref = atPeak (state == "offset" ? kick : src);
        const auto trk = atPeak (state == "offset" ? testutil::scaled (lp (150.0, testutil::delayed (kick, 0.0029 * fs)), -1.0)
                                                   : testutil::rotated (lp (6000.0, src), 115.0));

        if (state == "suggest")
        {
            feed (proc, kick, bass, 0, kick.size());
            proc.align();
            waitForResult (proc);
        }
        else if (state == "perhit" || state == "mapempty")
        {
            proc.param (ids::perHit).setValueNotifyingHost (1.f);
            feed (proc, kick, bass, 0, kick.size());
            if (state == "perhit")
            {
                proc.align();
                waitForResult (proc);
                feed (proc, kick, bass, 0, kick.size());                       // round again (a 4-bar window fills at the wrap)
                feed (proc, kick, bass, 0, (size_t) (1.15 * 240.0 / bpm * fs));   // and on into bar 2
            }
        }
        else if (state == "longmap")
        {
            proc.param (ids::perHit).setValueNotifyingHost (1.f);
            juce::MemoryBlock data;
            proc.getStateInformation (data);
            auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
            auto* m = xml->createNewChildElement ("Map");
            for (int beat = 0; beat < 96 * 4; ++beat)   // a hit on every beat, the turn wandering
            {
                const double deg = pf::wrapDeg (40.0 * std::sin (beat * 0.37) + (beat / 64) * 50.0);
                auto* pt = m->createNewChildElement ("Point");
                pt->setAttribute ("ppq", beat + 0.02);
                pt->setAttribute ("deg", deg);
                pt->setAttribute ("learnedDeg", deg);
                pt->setAttribute ("gainDb", 0.8);
                pt->setAttribute ("fit", 0.9);
                pt->setAttribute ("edited", 0);
            }
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
            proc.setStateInformation (data.getData(), (int) data.getSize());
            feed (proc, kick, bass, 0, kick.size());
        }
        else if (state == "export")
        {
            feed (proc, ref, trk, 0, src.size());
            proc.align();
            waitForResult (proc);
            proc.toggleExport();
            feed (proc, ref, trk, 0, src.size());   // the loop comes round: one pass
            feed (proc, ref, trk, 0, splitAtLoop ? 512 : src.size());   // and round again: done (run-through blocks: one more pass)
            for (int i = 0; i < 100 && proc.getExportInfo().stage != PhaseFixerProcessor::ExportStage::ready; ++i)
                pump (30);
            const auto e = proc.getExportInfo();
            std::printf ("export: stage %d, %.3f s from %s -> %s\n", (int) e.stage, e.seconds, e.where.toRawUTF8(), e.file.getFullPathName().toRawUTF8());
        }
        else if (state == "listening")
        {
            proc.align();
            feed (proc, ref, trk, 0, (size_t) (0.9 * fs));
            pump (200);
        }
        else if (state != "empty" && state != "roundtrip")
        {
            feed (proc, ref, trk, 0, src.size());
            if (state != "idle" && state != "nosc")
            {
                proc.align();
                waitForResult (proc);
            }
            if (state == "compare")
                proc.param (ids::compare).setValueNotifyingHost (1.f);
        }

        if (state == "roundtrip")
        {
            proc.param (ids::perHit).setValueNotifyingHost (1.f);
            feed (proc, kick, bass, 0, kick.size());
            proc.align();
            waitForResult (proc);
            if (proc.getMapSize() > 2) proc.editMapPoint (proc.getMap().points[2].ppq, 123.0);
            juce::MemoryBlock data;
            proc.getStateInformation (data);
            PhaseFixerProcessor copy;
            copy.setStateInformation (data.getData(), (int) data.getSize());
            const auto a = proc.getMap(), b = copy.getMap();
            bool same = a.size() == b.size() && a.size() > 0 && copy.isPerHit();
            for (int i = 0; same && i < a.size(); ++i)
                same = std::abs (a.points[(size_t) i].ppq - b.points[(size_t) i].ppq) < 1e-9
                    && std::abs (a.points[(size_t) i].deg - b.points[(size_t) i].deg) < 1e-9
                    && a.points[(size_t) i].edited == b.points[(size_t) i].edited;
            std::printf ("roundtrip: %d points, point 2 %.1f deg edited %d -> %s\n", b.size(), b.size() > 2 ? b.points[2].deg : 0.0,
                         b.size() > 2 ? (int) b.points[2].edited : 0, same ? "same" : "DIFFERENT");
            continue;
        }

        // PFSNAP_SPLIT=0 (above): blocks run through the loop's end.
        // PFSNAP_TRIGGER=0/1/2 (hit/beat/bar), PFSNAP_BARS=bars or PFSNAP_ZOOM=ms: scope settings for the shot.
        // PFSNAP_PHASE=degrees: the phase knob. PFSNAP_SCALE=1: render at 1x (default 2x, a Retina window).
        if (auto* t = std::getenv ("PFSNAP_TRIGGER")) proc.scopeTrigger = (PhaseFixerProcessor::Trigger) juce::jlimit (0, 2, juce::String (t).getIntValue());
        if (auto* z = std::getenv ("PFSNAP_ZOOM")) { proc.scopeZoomMs = juce::String (z).getFloatValue(); proc.scopeBars = 0.f; }
        if (auto* b = std::getenv ("PFSNAP_BARS")) proc.scopeBars = juce::String (b).getFloatValue();
        if (auto* ph = std::getenv ("PFSNAP_PHASE"))   // the phase knob (per hit: the offset), degrees
            proc.param (ids::phase).setValueNotifyingHost (proc.param (ids::phase).convertTo0to1 (juce::String (ph).getFloatValue()));
        std::unique_ptr<PhaseFixerEditor> editor (dynamic_cast<PhaseFixerEditor*> (proc.createEditor()));
        pump (100);
        for (int i = 0; i < 4; ++i) editor->tick();
        pump (50);

        if (state == "bench")
        {
            auto& scope = editor->getScope();
            for (float zoom : { 12.f, 100.f, 1000.f })
            {
                scope.setZoom (zoom);
                std::printf ("scope paint, %3.0f ms zoom: %6.2f ms native, %6.2f ms software\n", zoom, timePaint (scope, 40), timePaint (scope, 40, false));
            }
            scope.setZoom (1000.f);
            std::printf ("phase strip paint:       %6.2f ms\n", timePaint (*dynamic_cast<juce::Component*> (editor->getChildComponent (0)->getChildComponent (4)), 40));
            std::printf ("whole editor paint:      %6.2f ms\n", timePaint (*editor, 20));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 60; ++i) editor->tick();
            std::printf ("UI tick (no audio):      %6.3f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 60.0);
            continue;
        }

        if (state == "gestures")
        {
            failed = ! checkGestures (*editor, proc) || failed;
            continue;
        }
        if (state == "typing")
        {
            proc.param (ids::phase).setValueNotifyingHost (proc.param (ids::phase).convertTo0to1 (-12.5f));
            pump (20);
            doubleClick (*editor->getChildComponent (0), { 130.f, 524.f });
        }
        if (state == "panned")
        {
            auto& scope = editor->getScope();
            scope.setZoom (120.f);
            scroll (scope, { 400.f, 120.f }, -0.6f, 0.f);
            editor->tick();
            pump (50);
        }

        const float scale = std::getenv ("PFSNAP_SCALE") != nullptr ? juce::jlimit (1.f, 4.f, juce::String (std::getenv ("PFSNAP_SCALE")).getFloatValue()) : 2.f;
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outDir.getChildFile ("phasefixer_" + state + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);

        const auto st = proc.getAlignState();
        std::printf ("%s -> %s", state.toRawUTF8(), file.getFileName().toRawUTF8());
        if (st.hasResult)
            std::printf ("  [status %d phase %.1f invert %d offset %d %.2f ms timefix %.1f dB low %+.1f dB conf %.2f hits %d agree %.2f mapped %d hitconf %.2f]",
                         (int) st.result.status, st.result.correction.phaseDeg, (int) st.result.correction.invert, (int) st.result.hasOffset,
                         st.result.offsetSamples / fs * 1000.0, st.result.timeFixDb, st.result.lowEndGainDb, st.result.confidence,
                         (int) st.result.hits.size(), st.result.hitAgreement, st.mappedHits, st.result.hitConfidence);
        std::printf ("\n");
    }
    return failed ? 1 : 0;
}
