#pragma once

#include "align/Analyzer.h"
#include "align/Capture.h"
#include "align/Rotator.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr auto phase = "phase";
    inline constexpr auto invert = "invert";
    inline constexpr auto compare = "compare";
}

// The plugin sits on the track to be turned; the reference arrives on the
// sidechain. Both are kept (mono, uncorrected) in a Capture. ALIGN hands the
// last seconds of it to a worker thread running the analyzer, and the result
// is written into the parameters, so it is undoable and automatable like a
// hand edit. The track then runs through the Rotator: a PHA-979 style phase
// rotation with a fixed, host-compensated latency; nothing moves in time.
class PhaseFuckerProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    PhaseFuckerProcessor();
    ~PhaseFuckerProcessor() override;

    // ---- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter& param (const char* id) const { return *apvts.getParameter (id); }

    // The correction the parameters describe (any thread).
    pf::Correction correction() const noexcept;
    bool isComparing() const noexcept { return compareParam->load (std::memory_order_relaxed) > 0.5f; }

    // ---- Align (message thread) -----------------------------------------------
    // Press ALIGN: analyse now if enough shared signal was heard in the last
    // seconds, otherwise listen until there is. Pressing again while listening
    // analyses what is there (or stops, if that's too little).
    enum class Stage { idle, listening, analysing, done };
    struct AlignState
    {
        Stage stage = Stage::idle;
        float progress = 0.f;          // listening: shared seconds heard / needed
        bool hasResult = false;
        pf::AlignResult result;
        juce::int64 hitSample = -1;    // absolute Capture position of the result's hit
        juce::uint32 finishedAt = 0;   // Time::getMillisecondCounter() when the result came in
    };
    void align();
    AlignState getAlignState() const;
    void resetCorrection();
    static constexpr double kNeededSeconds = 2.0, kMinSeconds = 0.5, kWindowSeconds = 10.0;

    double getLatencyMs() const noexcept { return pf::Rotator::latencyFor (getRate()) * 1000.0 / getRate(); }

    // ---- For the editor -------------------------------------------------------
    // Runs f (const pf::Capture&) with the capture locked against being
    // reallocated by a sample-rate change.
    template <typename F>
    void withCapture (F&& f) const
    {
        const juce::ScopedReadLock l (captureLock);
        f (capture);
    }
    double getRate() const noexcept { return currentRate.load (std::memory_order_relaxed); }
    bool hasSidechainBus() const noexcept { return sidechainActive.load (std::memory_order_relaxed); }
    // Peak level since the last call, linear.
    float takePeak (bool reference) noexcept { return (reference ? refPeak : trkPeak).exchange (0.f, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void handleAsyncUpdate() override;
    void runAlignJob();
    void applyResult (const pf::AlignResult& r);
    void setParam (const char* id, float plainValue);

    struct Worker : juce::Thread
    {
        explicit Worker (PhaseFuckerProcessor& p) : juce::Thread ("PhaseFucker align"), owner (p) {}
        void run() override;
        PhaseFuckerProcessor& owner;
    };

    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* invertParam = nullptr;
    std::atomic<float>* compareParam = nullptr;

    pf::Capture capture;
    mutable juce::ReadWriteLock captureLock;   // writers: prepareToPlay only
    pf::Rotator rotator;
    juce::AudioBuffer<float> mono;   // [0] reference, [1] track
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool> sidechainActive { false };
    std::atomic<float> refPeak { 0.f }, trkPeak { 0.f };

    Worker worker { *this };
    std::atomic<bool> jobRequested { false }, analyseNow { false };
    mutable juce::CriticalSection stateLock;
    AlignState state;   // guarded by stateLock
    pf::AlignResult pendingResult;
    juce::int64 pendingHit = -1;
    bool pendingOk = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseFuckerProcessor)
};
