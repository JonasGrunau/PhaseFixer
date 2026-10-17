// Renders the editor offscreen to PNGs, so the UI can be reviewed without a DAW.
//   phasefucker_snapshot <out-dir> [state ...]
// States (default: all but bench):
//   empty      fresh instance, nothing played
//   idle       drums played through, not aligned yet
//   listening  ALIGN pressed with too little signal heard
//   aligned    a second mic, darker and 115 degrees turned: aligned
//   offset     kick in / kick out: the second mic 2.9 ms later and flipped
//   compare    aligned, A/B on
//   nosc       sidechain bus disabled
//   bench      aligned, then times scope paints at three zooms, the whole
//              editor, and UI ticks, on a native (CoreGraphics) image at 2x

#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
    void feed (PhaseFuckerProcessor& p, const testutil::Signal& ref, const testutil::Signal& trk, size_t from, size_t to)
    {
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
            p.processBlock (buf, midi);
        }
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
    if (states.isEmpty()) states = { "empty", "idle", "listening", "aligned", "offset", "compare", "nosc" };

    const double fs = 48000.0;
    const auto src = testutil::drums (fs, 8.0, 21);
    const auto darker = testutil::Biquad::lowpass (5000.0, 0.7, fs).zeroPhase (src);

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

        const auto trk = state == "offset" ? testutil::scaled (testutil::delayed (darker, 0.0029 * fs), -0.7)
                                           : testutil::scaled (testutil::rotated (darker, 115.0), 0.7);

        if (state == "listening")
        {
            proc.align();
            feed (proc, src, trk, 0, (size_t) (0.9 * fs));
            pump (200);
        }
        else if (state != "empty")
        {
            feed (proc, src, trk, 0, src.size());
            if (state != "idle" && state != "nosc")
            {
                proc.align();
                waitForResult (proc);
            }
            if (state == "compare")
                proc.param (ids::compare).setValueNotifyingHost (1.f);
        }

        std::unique_ptr<PhaseFuckerEditor> editor (dynamic_cast<PhaseFuckerEditor*> (proc.createEditor()));
        pump (100);
        for (int i = 0; i < 4; ++i) editor->tick();
        pump (50);

        if (state == "bench")
        {
            auto& scope = editor->getScope();
            for (float zoom : { 12.f, 50.f, 100.f })
            {
                scope.setZoom (zoom);
                std::printf ("scope paint, %3.0f ms zoom: %6.2f ms native, %6.2f ms software\n", zoom, timePaint (scope, 40), timePaint (scope, 40, false));
            }
            scope.setZoom (12.f);
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
            std::printf ("  [status %d phase %.1f invert %d offset %d %.2f ms timefix %.1f dB low %+.1f dB conf %.2f]", (int) st.result.status,
                         st.result.correction.phaseDeg, (int) st.result.correction.invert, (int) st.result.hasOffset,
                         st.result.offsetSamples / fs * 1000.0, st.result.timeFixDb, st.result.lowEndGainDb, st.result.confidence);
        std::printf ("\n");
    }
    return 0;
}
