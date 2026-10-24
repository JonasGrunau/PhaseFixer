// PhaseFucker without a DAW: plays a reference and a second "mic" of the same
// sound through the plugin (reference on the sidechain, as in a session) and
// out of the speakers, with the plugin's editor below a small transport bar.
//   phasefucker_demo [reference.wav track.wav]
//
// SUM plays reference + corrected track with the reference delayed by the
// plugin's latency, the way a DAW compensates it; that is where alignment is
// heard. TRACK and REFERENCE play one side alone. Space plays and stops.
// The built-in material is a psytrance kick and rolling bass: real samples
// from a local kit file when PF_DEMO_KIT names one (DemoKit.h; start.sh sets
// it for demo-kit.txt), otherwise synthesized (PsyLoop.h). The plugin gets a
// matching looping transport, so the scope's BEAT and BAR triggers, PER HIT
// and export work as in a DAW.

#include "DemoKit.h"
#include "PsyLoop.h"
#include "TempoPlayHead.h"
#include "TestUtil.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

using namespace theme;

namespace
{
    // Two stereo signals of equal length at one sample rate, looped.
    struct Material
    {
        juce::String name, note;
        double rate = 0.0, bpm = 120.0;
        bool hasTempo = false;   // built-in loops: `bpm` from sample 0
        juce::AudioBuffer<float> ref, trk;
    };

    juce::AudioBuffer<float> stereo (const testutil::Signal& s, float gain)
    {
        juce::AudioBuffer<float> b (2, (int) s.size());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < (int) s.size(); ++i)
                b.setSample (c, i, gain * s[(size_t) i]);
        return b;
    }

    const juce::StringArray kScenarios {
        "Kick + bass: changes under the kick",
        "Kick + bass: the same every beat",
        "Kick layers: sub 2.9 ms late, flipped",
        "Kick + bass, second chain: turned 115 degrees"
    };

    std::shared_ptr<Material> makeScenario (int which, double fs)
    {
        auto m = std::make_shared<Material>();
        m->rate = fs;
        m->hasTempo = true;
        m->name = kScenarios[which];
        const auto kit = kit::load();
        const auto loop = kit ? kit::make (*kit, fs, which != 1) : psy::make (fs, which != 1);
        m->bpm = kit ? kit->bpm : psy::kBpm;
        const juce::String source = kit ? "Kit " + kit->kick.getFileNameWithoutExtension() + " + " + kit->bass[0].getFileNameWithoutExtension() + ". "
                                        : juce::String ("Synthesized (no PF_DEMO_KIT). ");
        const auto lp = [&] (double hz, const testutil::Signal& x) { return testutil::Biquad::lowpass (hz, 0.7, fs).zeroPhase (x); };
        testutil::Signal ref, trk;
        switch (which)
        {
            case 0:
                ref = loop.kick;
                trk = loop.bass;
                m->note = source + (kit ? "The bass changes notes under the kick, so not every kick meets it the same way. PER HIT, then ALIGN."
                                        : "The bass oscillator runs free, so every kick meets it at another phase. PER HIT, then ALIGN.");
                break;
            case 1:
                ref = loop.kick;
                trk = loop.bass;
                m->note = source + "Kick and bass meet the same way every beat: ONE TURN fits.";
                break;
            case 2:
                ref = loop.kick;
                trk = testutil::scaled (lp (150.0, testutil::delayed (loop.kick, 0.0029 * fs)), -0.9);
                m->note = source + "The kick's sub layer, 2.9 ms late and flipped: the turn fixes the low end, the time anchor says what a move adds.";
                break;
            default:
                ref = testutil::sum (loop.kick, loop.bass);
                trk = testutil::scaled (testutil::rotated (lp (6000.0, ref), 115.0), 0.7);
                m->note = source + "The whole loop through a phase-turning chain: ALIGN should land on flip + 65 degrees.";
                break;
        }
        // Peaks to -3 dBFS: a turn raises a punchy kick's peaks, and the inputs shouldn't clip.
        const auto peak = [] (const testutil::Signal& x) { float p = 1e-6f; for (float v : x) p = std::max (p, std::abs (v)); return p; };
        m->ref = stereo (ref, 0.71f / peak (ref));
        m->trk = stereo (trk, 0.71f / peak (trk));
        return m;
    }

    std::shared_ptr<Material> makeFromFiles (const juce::File& ref, const juce::File& trk, double fs)
    {
        auto m = std::make_shared<Material>();
        m->rate = fs;
        if (! kit::readFile (ref, fs, m->ref) || ! kit::readFile (trk, fs, m->trk)) return nullptr;
        const int n = std::min (m->ref.getNumSamples(), m->trk.getNumSamples());
        m->ref.setSize (2, n, true);
        m->trk.setSize (2, n, true);
        m->name = ref.getFileName() + " + " + trk.getFileName();
        m->note = "Your files: the first is the reference (sidechain), the second the track.";
        return m;
    }
} // namespace

// Feeds the processor from the device callback, like a host would.
class DemoAudio : public juce::AudioIODeviceCallback
{
public:
    enum Monitor { sum, track, reference };

    explicit DemoAudio (PhaseFuckerProcessor& p) : proc (p) { proc.setPlayHead (&playHead); }

    std::function<void (double)> onRate;   // message thread: the device runs at a new rate
    std::atomic<int> monitor { sum };
    std::atomic<bool> playing { true };

    void setMaterial (std::shared_ptr<Material> m)
    {
        {
            const juce::SpinLock::ScopedLockType l (lock);
            material = std::move (m);
            pos = 0;
        }
        proc.restartListening();
    }

    double rate() const noexcept { return fs.load(); }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        start (d->getCurrentSampleRate(), std::max (16, d->getCurrentBufferSizeSamples()));
    }

    void start (double rate, int block)
    {
        proc.setRateAndBufferSizeDetails (rate, block);
        proc.prepareToPlay (rate, block);
        capacity = block;
        work.setSize (4, block);
        dry.setSize (2, block);
        delay.setSize (2, 1 << 15);
        delay.clear();
        delayPos = 0;
        fs = rate;
        juce::MessageManager::callAsync ([this, rate] { if (onRate) onRate (rate); });
    }

    void audioDeviceStopped() override { proc.releaseResources(); }

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

        const juce::SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked() || material == nullptr || std::abs (material->rate - fs.load()) > 0.5) return;
        if (! playing)
        {
            // Stopped: hosts keep processing, with the transport standing.
            juce::MidiBuffer midi;
            for (int done = 0; done < n; done += capacity)
            {
                work.setSize (4, std::min (capacity, n - done), false, false, true);
                work.clear();
                playHead.playing = false;
                proc.processBlock (work, midi);
            }
            playHead.playing = true;
            return;
        }

        const auto& m = *material;
        const int len = m.ref.getNumSamples();
        const int latency = proc.getLatencySamples();
        const int mask = delay.getNumSamples() - 1;
        juce::MidiBuffer midi;

        for (int done = 0; done < n;)
        {
            const int chunk = std::min ({ n - done, capacity, len - (int) pos });
            work.setSize (4, chunk, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                work.copyFrom (c, 0, m.trk, c, (int) pos, chunk);
                work.copyFrom (c + 2, 0, m.ref, c, (int) pos, chunk);
                dry.copyFrom (c, 0, m.ref, c, (int) pos, chunk);
            }
            playHead.sampleRate = fs.load();
            playHead.position = pos;
            playHead.hasTempo = m.hasTempo;
            playHead.bpm = m.bpm;
            playHead.loopQuarters = len / fs.load() * m.bpm / 60.0;
            proc.processBlock (work, midi);

            const int mon = monitor.load();
            for (int i = 0; i < chunk; ++i)
            {
                for (int c = 0; c < 2; ++c)
                    delay.setSample (c, (delayPos + i) & mask, dry.getSample (c, i));
                for (int c = 0; c < std::min (numOut, 2); ++c)
                {
                    if (out[c] == nullptr) continue;
                    // The reference as the host would play it: late by the plugin's latency.
                    const float r = delay.getSample (c, (delayPos + i - latency) & mask);
                    const float t = work.getSample (c, i);
                    out[c][done + i] = mon == sum ? 0.5f * (r + t) : mon == track ? 0.7f * t : 0.7f * r;
                }
            }
            delayPos = (delayPos + chunk) & mask;
            pos = (pos + chunk) % len;
            done += chunk;
        }
    }

private:
    PhaseFuckerProcessor& proc;
    TempoPlayHead playHead;
    juce::SpinLock lock;
    std::shared_ptr<Material> material;
    juce::int64 pos = 0;
    std::atomic<double> fs { 0.0 };
    juce::AudioBuffer<float> work, dry, delay;
    int capacity = 0, delayPos = 0;
};

// House-style buttons and menus for the transport bar.
class BarLookAndFeel : public SheetLookAndFeel
{
public:
    BarLookAndFeel()
    {
        setColour (juce::TextButton::buttonColourId, col::sheet);
        setColour (juce::TextButton::buttonOnColourId, col::ink);
        setColour (juce::TextButton::textColourOffId, col::ink);
        setColour (juce::TextButton::textColourOnId, col::sheet);
        setColour (juce::ComboBox::backgroundColourId, col::sheet);
        setColour (juce::ComboBox::textColourId, col::ink);
        setColour (juce::ComboBox::outlineColourId, col::inkSoft);
        setColour (juce::ComboBox::arrowColourId, col::ink);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return label (10.f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return mono (11.f); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f).translated (0.f, down ? 1.f : 0.f);
        if (b.getToggleState())
        {
            g.setColour (col::ink);
            g.fillRoundedRectangle (r, kRadius);
        }
        else
        {
            g.setColour (over ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (r, kRadius, 1.f);
        }
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour (col::inkSoft);
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f), kRadius, 1.f);
        g.setColour (col::ink);
        juce::Path arrow;
        arrow.addTriangle ((float) w - 16.f, (float) h * 0.5f - 2.f, (float) w - 8.f, (float) h * 0.5f - 2.f, (float) w - 12.f, (float) h * 0.5f + 3.f);
        g.fillPath (arrow);
    }
};

class DemoComponent : public juce::Component
{
public:
    static constexpr int kBar = 52;

    DemoComponent (juce::File refFile, juce::File trkFile)
        : audio (proc), files { refFile, trkFile }
    {
        if (files[0].existsAsFile() && files[1].existsAsFile()) current = 100;
        setLookAndFeel (&lnf);
        setOpaque (true);
        editor.reset (dynamic_cast<PhaseFuckerEditor*> (proc.createEditor()));
        addAndMakeVisible (*editor);

        // Space plays and stops from anywhere, so the controls don't take the focus.
        play.setWantsKeyboardFocus (false);
        source.setWantsKeyboardFocus (false);
        play.setClickingTogglesState (true);
        play.setToggleState (true, juce::dontSendNotification);
        play.onClick = [this] { audio.playing = play.getToggleState(); play.setButtonText (play.getToggleState() ? "Stop" : "Play"); };
        play.setButtonText ("Stop");
        addAndMakeVisible (play);

        for (int i = 0; i < kScenarios.size(); ++i) source.addItem (kScenarios[i], i + 1);
        source.addSeparator();
        source.addItem (juce::String (juce::CharPointer_UTF8 ("Your files\xe2\x80\xa6")), 100);
        source.onChange = [this] { chooseSource (source.getSelectedId()); };
        addAndMakeVisible (source);

        const char* names[] = { "Sum", "Track", "Reference" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = monitor[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (1);
            b.setWantsKeyboardFocus (false);
            b.onClick = [this, i] { if (monitor[(size_t) i].getToggleState()) audio.monitor = i; };
            addAndMakeVisible (b);
        }
        monitor[0].setToggleState (true, juce::dontSendNotification);

        audio.onRate = [this] (double) { reload(); };
        const auto err = devices.initialiseWithDefaultDevices (0, 2);
        if (err.isNotEmpty()) note = "Audio device: " + err;
        devices.addAudioCallback (&audio);

        setWantsKeyboardFocus (true);
        setSize (PhaseFuckerEditor::kWidth, PhaseFuckerEditor::kHeight + kBar);
    }

    ~DemoComponent() override
    {
        devices.removeAudioCallback (&audio);
        editor.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        auto bar = getLocalBounds().removeFromTop (kBar).toFloat();
        g.setColour (col::hairline);
        g.fillRect (bar.removeFromBottom (1.f));
        caption (g, "Demo host", { 24.f, 6.f, 100.f, 12.f }, juce::Justification::centredLeft, col::accent, 9.f);
        g.setFont (mono (10.f));
        g.setColour (col::inkSoft);
        g.drawText (note, juce::Rectangle<float> (600.f, 8.f, (float) getWidth() - 624.f, 36.f), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        play.setBounds (24, 20, 64, 24);
        source.setBounds (98, 20, 250, 24);
        for (int i = 0; i < 3; ++i)
            monitor[(size_t) i].setBounds (362 + i * 74, 20, i == 2 ? 86 : 70, 24);
        editor->setBounds (0, kBar, PhaseFuckerEditor::kWidth, PhaseFuckerEditor::kHeight);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey) { play.triggerClick(); return true; }
        return false;
    }

    void visibilityChanged() override
    {
        if (isShowing()) grabKeyboardFocus();
    }

private:
    // (Re)builds the current material at the device's rate.
    void reload()
    {
        const double fs = audio.rate();
        if (fs <= 0.0) return;
        std::shared_ptr<Material> m;
        if (current == 100 && files[0].existsAsFile() && files[1].existsAsFile())
            m = makeFromFiles (files[0], files[1], fs);
        if (m == nullptr)
        {
            if (current == 100) current = 1;
            m = makeScenario (current - 1, fs);
        }
        note = m->note;
        source.setSelectedId (current, juce::dontSendNotification);
        audio.setMaterial (std::move (m));
        repaint (0, 0, getWidth(), kBar);
    }

    void chooseSource (int id)
    {
        if (id != 100)
        {
            current = id;
            reload();
            return;
        }
        // Two choosers in a row: reference first, then the track.
        chooser = std::make_unique<juce::FileChooser> ("Reference (goes to the sidechain)", files[0], "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& a) {
            const auto ref = a.getResult();
            if (! ref.existsAsFile()) { source.setSelectedId (current, juce::dontSendNotification); return; }
            chooser = std::make_unique<juce::FileChooser> ("Track (the one PhaseFucker corrects)", ref.getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, ref] (const juce::FileChooser& b) {
                const auto trk = b.getResult();
                if (trk.existsAsFile()) { files = { ref, trk }; current = 100; }
                reload();
            });
        });
    }

    PhaseFuckerProcessor proc;
    DemoAudio audio;
    juce::AudioDeviceManager devices;
    BarLookAndFeel lnf;
    std::unique_ptr<PhaseFuckerEditor> editor;
    juce::TextButton play;
    juce::ComboBox source;
    std::array<juce::TextButton, 3> monitor;
    std::array<juce::File, 2> files;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String note { "Starting audio..." };
    int current = 1;
};

class DemoApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "PhaseFucker Demo"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

    void initialise (const juce::String& args) override
    {
        const auto parts = juce::StringArray::fromTokens (args, true);
        const auto file = [&] (int i) { return i < parts.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (parts[i].unquoted()) : juce::File(); };
        window = std::make_unique<Window> (std::make_unique<DemoComponent> (file (0), file (1)));
    }

    void shutdown() override { window.reset(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (std::unique_ptr<DemoComponent> content)
            : DocumentWindow ("PhaseFucker Demo", col::sheet, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };

    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (DemoApp)
