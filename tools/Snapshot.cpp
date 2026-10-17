// Renders the editor offscreen to PNGs, so the UI can be reviewed without a DAW.
//   phasefucker_snapshot <out-dir> [state ...]
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
//   export     aligned, one pass recorded and rendered (into <out-dir>)
//   roundtrip  per hit, aligned, one point edited; saves the session into a
//              fresh instance and checks the map came back (no image)
//   bench      aligned, then times scope paints at three zooms, the whole
//              editor, and UI ticks, on a native (CoreGraphics) image at 2x

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

    void feed (PhaseFuckerProcessor& p, const testutil::Signal& ref, const testutil::Signal& trk, size_t from, size_t to)
    {
        p.setPlayHead (&playHead);
        playHead.sampleRate = p.getRate();
        playHead.playing = true;
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
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
    void stop (PhaseFuckerProcessor& p)
    {
        playHead.playing = false;
        juce::AudioBuffer<float> buf (4, 512);
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        playHead.playing = true;
    }

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    void waitForResult (PhaseFuckerProcessor& p)
    {
        for (int i = 0; i < 200; ++i)
        {
            pump (25);
            if (p.getAlignState().stage == PhaseFuckerProcessor::Stage::done) return;
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
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    juce::StringArray states;
    for (int i = 2; i < argc; ++i) states.add (argv[i]);
    if (states.isEmpty()) states = { "empty", "idle", "listening", "aligned", "offset", "compare", "nosc", "suggest", "perhit", "mapempty", "export" };

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

    for (const auto& state : states)
    {
        PhaseFuckerProcessor proc;
        if (state == "nosc")
        {
            auto layout = proc.getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::disabled();
            proc.setBusesLayout (layout);
        }
        proc.prepareToPlay (fs, 512);
        proc.exportDirectory = outDir;
        playHead.bpm = bpm;
        playHead.loopQuarters = 16.0;

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
        else if (state == "export")
        {
            feed (proc, ref, trk, 0, src.size());
            proc.align();
            waitForResult (proc);
            proc.toggleExport();
            feed (proc, ref, trk, 0, src.size());   // the loop comes round: one pass
            feed (proc, ref, trk, 0, 512);          // and round again: done
            for (int i = 0; i < 100 && proc.getExportInfo().stage != PhaseFuckerProcessor::ExportStage::ready; ++i)
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
            proc.editMapPoint (2, 123.0);
            juce::MemoryBlock data;
            proc.getStateInformation (data);
            PhaseFuckerProcessor copy;
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

        // PFSNAP_TRIGGER=0/1/2 (hit/beat/bar), PFSNAP_BARS=bars or PFSNAP_ZOOM=ms: scope settings for the shot.
        if (auto* t = std::getenv ("PFSNAP_TRIGGER")) proc.scopeTrigger = (PhaseFuckerProcessor::Trigger) juce::jlimit (0, 2, juce::String (t).getIntValue());
        if (auto* z = std::getenv ("PFSNAP_ZOOM")) { proc.scopeZoomMs = juce::String (z).getFloatValue(); proc.scopeBars = 0.f; }
        if (auto* b = std::getenv ("PFSNAP_BARS")) proc.scopeBars = juce::String (b).getFloatValue();
        std::unique_ptr<PhaseFuckerEditor> editor (dynamic_cast<PhaseFuckerEditor*> (proc.createEditor()));
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

        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.f);
        const auto file = outDir.getChildFile ("phasefucker_" + state + ".png");
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
    return 0;
}
