#pragma once

#include "align/Analyzer.h"
#include "align/Capture.h"
#include "align/PhaseMap.h"
#include "align/Rotator.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr auto phase = "phase";
    inline constexpr auto invert = "invert";
    inline constexpr auto compare = "compare";
    inline constexpr auto perHit = "perhit";
}

// The plugin sits on the track to be turned; the reference arrives on the
// sidechain. Both are kept (mono, uncorrected) in a Capture. ALIGN hands the
// last seconds of it to a worker thread running the analyzer, and the result
// is written into the parameters, so it is undoable and automatable like a
// hand edit. The track then runs through the Rotator: a PHA-979 style phase
// rotation with a fixed, host-compensated latency; nothing moves in time.
//
// Per hit (the `perhit` parameter), ALIGN instead learns a turn for every hit
// of the reference into a PhaseMap on the host's timeline, played back with
// the transport; the phase knob then adds an offset to the whole map.
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
    double getTailLengthSeconds() const override { return pf::Rotator::tailFor (getRate()) / getRate(); }

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
    bool isPerHit() const noexcept { return perHitParam->load (std::memory_order_relaxed) > 0.5f; }

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

        bool perHit = false;           // the result went into the phase map
        int mappedHits = 0;            // hits it added (or renewed) there
        bool noTimeline = false;       // per hit, but the host's transport wasn't running
    };
    void align();
    AlignState getAlignState() const;
    void resetCorrection();
    // Forget what was heard so far (the source changed). Any thread; takes
    // effect at the next audio block.
    void restartListening() noexcept { restartRequested = true; }
    static constexpr double kNeededSeconds = 2.0, kMinSeconds = 0.5, kWindowSeconds = 10.0;

    double getLatencyMs() const noexcept { return pf::Rotator::latencyFor (getRate()) * 1000.0 / getRate(); }

    // ---- Phase map (message thread) -------------------------------------------
    pf::PhaseMap getMap() const;
    int getMapSize() const;
    int getMapRevision() const noexcept { return mapRevision.load (std::memory_order_relaxed); }
    // Sets the turn of the point at `ppq` (as getMap() gave it; by position,
    // so a map ALIGN renewed meanwhile can't send the edit to another point).
    void editMapPoint (double ppq, double deg);
    void clearMap();
    // What the audio thread plays right now: the total turn (offset and
    // polarity included) and the map point it comes from (-1: none).
    double getLiveTurn() const noexcept { return liveTurn.load (std::memory_order_relaxed); }
    int getLiveMapIndex() const noexcept { return liveMapIndex.load (std::memory_order_relaxed); }

    // ---- Export (message thread) ----------------------------------------------
    // One pass of the corrected track as a 24-bit WAV, to drag into the
    // arrangement. Recording starts when playback starts or the loop comes
    // round, and ends when it stops or comes round again (max kExportSeconds).
    // The file is rendered offline through the same chain, so it holds exactly
    // what was heard, minus the latency.
    enum class ExportStage { idle, armed, recording, rendering, ready, failed };
    struct ExportInfo
    {
        ExportStage stage = ExportStage::idle;
        double seconds = 0.0;      // recorded so far, or the file's length
        juce::String where;        // "bar 5", where the file starts
        juce::File file;
    };
    // idle / ready / failed: arm. armed: cancel. recording: stop and render.
    void toggleExport();
    ExportInfo getExportInfo() const;
    static constexpr double kExportSeconds = 90.0;
    // Where exports go (kept, not temporary: a host may reference the file
    // where it lies). Set before use; the dev tools point it elsewhere.
    juce::File exportDirectory = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PhaseFucker");

    void updateTrackProperties (const TrackProperties& p) override;

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

    // Where the host's timeline was at the latest block, tied to a capture
    // position, so the scope can trigger on beats and bars.
    struct Transport
    {
        bool valid = false;               // playing, with a tempo and a position
        juce::int64 index = 0;            // capture position of the block's first sample
        double ppq = 0.0, bpm = 120.0, barPpq = 0.0;
        int numerator = 4, denominator = 4;
        juce::int64 contiguousFrom = 0;   // capture position since which the timeline ran without a jump (a loop coming round is none)
        bool looping = false;
        double loopStart = 0.0, loopEnd = 0.0;
        int wrapAt = -1;                  // sample of the block where the loop comes round (0: at its start), -1: none
    };
    Transport getTransport() const
    {
        const juce::SpinLock::ScopedLockType l (transportLock);
        return transport;
    }

    // Scope settings (message thread), saved with the session.
    enum class Trigger { hit, beat, bar };
    Trigger scopeTrigger = Trigger::bar;
    float scopeZoomMs = 1000.f;
    float scopeBars = 0.5f;   // window length in bars (follows the tempo); 0: free zoom (scopeZoomMs)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void handleAsyncUpdate() override;
    void runAlignJob();
    void applyResult (const pf::AlignResult& r);
    void learnMap();
    void mapChanged();   // by hand or by ALIGN: redraw, and tell the host the session changed
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
    std::atomic<float>* perHitParam = nullptr;

    pf::Capture capture;
    mutable juce::ReadWriteLock captureLock;   // writers: prepareToPlay only
    pf::Rotator rotator;
    juce::AudioBuffer<float> mono;   // [0] reference, [1] track
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool> sidechainActive { false };
    std::atomic<float> refPeak { 0.f }, trkPeak { 0.f };

    Transport readTransport (juce::int64 index, int numSamples) noexcept;
    double turnFor (const Transport& t, int numSamples) noexcept;
    void recordForExport (const juce::AudioBuffer<float>& in, const Transport& t, double turn) noexcept;
    void renderExport();
    mutable juce::SpinLock transportLock;
    Transport transport;                  // guarded by transportLock
    double lastPpq = 0.0, lastBpm = 0.0;  // audio thread
    juce::int64 lastIndex = 0, contiguousFrom = 0;
    bool lastValid = false;

    // Phase map: edited on the message thread, read by the audio thread with
    // a try-lock (if it misses, the last turn holds for a block).
    mutable juce::SpinLock mapLock;
    pf::PhaseMap map;                       // guarded by mapLock
    std::atomic<int> mapRevision { 0 }, liveMapIndex { -1 };
    std::atomic<double> liveTurn { 0.0 };
    double mapTurn = 0.0;                   // audio thread: the map's last turn

    // Export: buffers are sized on the message thread before arming; the
    // audio thread fills them while recording; the worker renders.
    std::atomic<int> exportStage { (int) ExportStage::idle };
    std::atomic<bool> exportStopRequested { false };
    juce::AudioBuffer<float> recBuffer;
    std::vector<pf::Turn> recTurns;
    std::atomic<int> recLength { 0 };
    int recChannels = 1;
    double recStartPpq = 0.0, recBpm = 120.0, recBarPpq = 0.0, recLoopEnd = -1.0;
    int recNumerator = 4, recDenominator = 4;
    bool recLooped = false, recFromLoopStart = false;
    juce::File exportFile;                  // guarded by stateLock
    juce::String trackName;                 // guarded by stateLock

    Worker worker { *this };
    std::atomic<bool> jobRequested { false }, analyseNow { false }, restartRequested { false };
    mutable juce::CriticalSection stateLock;
    AlignState state;   // guarded by stateLock
    pf::AlignResult pendingResult;
    juce::int64 pendingHit = -1;
    bool pendingOk = false;
    std::vector<pf::PhaseMap::Hit> pendingHits;   // per hit: placed on the timeline
    double pendingFrom = 0.0, pendingTo = 0.0, pendingBpm = 120.0;
    bool pendingPerHit = false;
    juce::String exportWhere;               // guarded by stateLock
    double exportSeconds = 0.0;             // guarded by stateLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhaseFuckerProcessor)
};
