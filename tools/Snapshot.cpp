// Renders the editor offscreen to PNGs, so the UI can be reviewed without a DAW.
//   phasefucker_snapshot <out-dir> [state ...]
// States (default: all):
//   empty      fresh instance, nothing played
//   idle       drums played through, not aligned yet
//   listening  ALIGN pressed with too little signal heard
//   aligned    drums played through and aligned
//   rotate     a phase-shifted reference: aligned with the allpass
//   compare    aligned, A/B on
//   nosc       sidechain bus disabled
// The track is the reference delayed by 2.9 ms, flipped and darker, like a
// second mic on the same drum.

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
            const auto st = p.getAlignState();
            if (st.stage == PhaseFuckerProcessor::Stage::done) return;
        }
        std::printf ("timed out waiting for the analysis\n");
    }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    juce::StringArray states;
    for (int i = 2; i < argc; ++i) states.add (argv[i]);
    if (states.isEmpty()) states = { "empty", "idle", "listening", "aligned", "rotate", "compare", "nosc" };

    const double fs = 48000.0;
    const auto src = testutil::drums (fs, 8.0, 21);

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

        auto ref = src;
        if (state == "rotate") ref = testutil::allpassed (src, 140.0, 0.8, fs);
        const auto trk = testutil::scaled (testutil::Biquad::lowpass (5000.0, 0.7, fs).zeroPhase (testutil::delayed (src, 0.0029 * fs)), -0.7);

        std::unique_ptr<PhaseFuckerEditor> editor;
        if (state == "listening")
        {
            proc.align();
            feed (proc, ref, trk, 0, (size_t) (0.9 * fs));
            pump (200);
        }
        else if (state != "empty")
        {
            feed (proc, ref, trk, 0, src.size());
            if (state != "idle" && state != "nosc")
            {
                proc.align();
                waitForResult (proc);
            }
            if (state == "compare")
            {
                auto& p = proc.param (ids::compare);
                p.setValueNotifyingHost (1.f);
            }
        }

        editor.reset (dynamic_cast<PhaseFuckerEditor*> (proc.createEditor()));
        pump (100);
        for (int i = 0; i < 4; ++i) editor->tick();
        pump (50);

        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.f);
        const auto file = outDir.getChildFile ("phasefucker_" + state + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);

        const auto st = proc.getAlignState();
        std::printf ("%s -> %s", state.toRawUTF8(), file.getFullPathName().toRawUTF8());
        if (st.hasResult)
            std::printf ("  [status %d shift %.3f ms invert %d rotate %d %.0f Hz conf %.2f]", (int) st.result.status, st.result.correction.shiftMs,
                         (int) st.result.correction.invert, (int) st.result.correction.rotate, st.result.correction.rotateHz, st.result.confidence);
        std::printf ("\n");
        editor.reset();
    }
    return 0;
}
