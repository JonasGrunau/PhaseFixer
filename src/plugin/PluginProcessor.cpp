#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
    }
} // namespace

PhaseFuckerProcessor::PhaseFuckerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Reference", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseFucker", createLayout())
{
    shiftParam = apvts.getRawParameterValue (ids::shift);
    invertParam = apvts.getRawParameterValue (ids::invert);
    rotateParam = apvts.getRawParameterValue (ids::rotate);
    rotateFreqParam = apvts.getRawParameterValue (ids::rotateFreq);
    rotateQParam = apvts.getRawParameterValue (ids::rotateQ);
    compareParam = apvts.getRawParameterValue (ids::compare);
    apvts.addParameterListener (ids::shift, this);

    capture.prepare (48000.0);
    aligner.prepare (48000.0, 2);
    worker.startThread (juce::Thread::Priority::low);
    startTimerHz (10);
}

PhaseFuckerProcessor::~PhaseFuckerProcessor()
{
    stopTimer();
    apvts.removeParameterListener (ids::shift, this);
    worker.signalThreadShouldExit();
    worker.notify();
    worker.stopThread (4000);
}

juce::AudioProcessorValueTreeState::ParameterLayout PhaseFuckerProcessor::createLayout()
{
    using namespace juce;
    const auto ms = [] (float v, int) {
        return String (v, std::abs (v) < 10.f ? 3 : 2) + " ms";
    };
    const auto hz = [] (float v, int) {
        return v < 1000.f ? (v < 100.f ? String (v, 1) : String (roundToInt (v))) + " Hz" : String (v / 1000.f, 2) + " kHz";
    };

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::shift, 1 }, "Shift",
        NormalisableRange<float> (-(float) pf::limits::kMaxShiftMs, (float) pf::limits::kMaxShiftMs, 0.f, 0.35f, true), 0.f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (ms)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::invert, 1 }, "Polarity", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::rotate, 1 }, "Rotate", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::rotateFreq, 1 }, "Rotate Freq",
        logRange ((float) pf::limits::kMinRotateHz, (float) pf::limits::kMaxRotateHz), 200.f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hz)));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::rotateQ, 1 }, "Rotate Q",
        logRange ((float) pf::limits::kMinRotateQ, (float) pf::limits::kMaxRotateQ), 0.7f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::compare, 1 }, "Compare", false));
    return layout;
}

pf::Correction PhaseFuckerProcessor::correction() const noexcept
{
    pf::Correction c;
    c.shiftMs = shiftParam->load (std::memory_order_relaxed);
    c.invert = invertParam->load (std::memory_order_relaxed) > 0.5f;
    c.rotate = rotateParam->load (std::memory_order_relaxed) > 0.5f;
    c.rotateHz = rotateFreqParam->load (std::memory_order_relaxed);
    c.rotateQ = rotateQParam->load (std::memory_order_relaxed);
    return c;
}

// ---- Audio --------------------------------------------------------------------

bool PhaseFuckerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out || (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()))
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void PhaseFuckerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - capture.sampleRate()) > 0.5;
    currentRate.store (sampleRate, std::memory_order_relaxed);
    if (rateChanged)
    {
        const juce::ScopedWriteLock l (captureLock);
        capture.prepare (sampleRate);
    }
    aligner.prepare (sampleRate, std::max (1, getMainBusNumInputChannels()));
    mono.setSize (2, std::max (1, samplesPerBlock));
    setLatencySamples (pf::Aligner::latencyFor (getLookaheadMs(), sampleRate));
}

void PhaseFuckerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const auto* sideBus = getBus (true, 1);
    const bool hasSide = sideBus != nullptr && sideBus->isEnabled() && sideBus->getNumberOfChannels() > 0;
    sidechainActive.store (hasSide, std::memory_order_relaxed);

    // Mono sums for the capture, in chunks the scratch buffer can hold.
    const int chunk = mono.getNumSamples();
    float pr = 0.f, pt = 0.f;
    for (int start = 0; start < n; start += chunk)
    {
        const int len = std::min (chunk, n - start);
        float* r = mono.getWritePointer (0);
        float* t = mono.getWritePointer (1);
        juce::FloatVectorOperations::clear (t, len);
        for (int c = 0; c < main.getNumChannels(); ++c)
            juce::FloatVectorOperations::addWithMultiply (t, main.getReadPointer (c, start), 1.f / (float) main.getNumChannels(), len);
        if (hasSide)
        {
            auto side = getBusBuffer (buffer, true, 1);
            juce::FloatVectorOperations::clear (r, len);
            for (int c = 0; c < side.getNumChannels(); ++c)
                juce::FloatVectorOperations::addWithMultiply (r, side.getReadPointer (c, start), 1.f / (float) side.getNumChannels(), len);
            pr = std::max (pr, juce::FloatVectorOperations::findMaximum (r, len));
            pr = std::max (pr, -juce::FloatVectorOperations::findMinimum (r, len));
        }
        pt = std::max (pt, juce::FloatVectorOperations::findMaximum (t, len));
        pt = std::max (pt, -juce::FloatVectorOperations::findMinimum (t, len));
        capture.push (hasSide ? r : nullptr, t, len);
    }
    if (pr > refPeak.load (std::memory_order_relaxed)) refPeak.store (pr, std::memory_order_relaxed);
    if (pt > trkPeak.load (std::memory_order_relaxed)) trkPeak.store (pt, std::memory_order_relaxed);

    aligner.setTarget (correction(), lookaheadMs.load (std::memory_order_relaxed), isComparing());
    shiftClamped.store (aligner.isShiftClamped(), std::memory_order_relaxed);
    aligner.process (main.getArrayOfWritePointers(), main.getNumChannels(), n);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);
}

// ---- Align ----------------------------------------------------------------------

void PhaseFuckerProcessor::align()
{
    {
        const juce::ScopedLock sl (stateLock);
        if (state.stage == Stage::analysing) return;
        if (state.stage == Stage::listening)
        {
            analyseNow = true;
            worker.notify();
            return;
        }
        state.stage = Stage::listening;
        state.progress = 0.f;
    }
    analyseNow = false;
    jobRequested = true;
    worker.notify();
}

PhaseFuckerProcessor::AlignState PhaseFuckerProcessor::getAlignState() const
{
    const juce::ScopedLock sl (stateLock);
    return state;
}

void PhaseFuckerProcessor::Worker::run()
{
    while (! threadShouldExit())
    {
        wait (-1);
        if (threadShouldExit()) return;
        if (owner.jobRequested.exchange (false))
            owner.runAlignJob();
    }
}

void PhaseFuckerProcessor::runAlignJob()
{
    // Listen until there is enough shared signal (or ALIGN is pressed again).
    for (;;)
    {
        if (worker.threadShouldExit()) return;
        pf::Capture::Activity act;
        withCapture ([&] (const pf::Capture& c) { act = c.activity (kWindowSeconds); });
        const bool now = analyseNow.load();
        {
            const juce::ScopedLock sl (stateLock);
            state.progress = (float) juce::jlimit (0.0, 1.0, act.shared / kNeededSeconds);
        }
        if (act.shared >= kNeededSeconds || (now && act.shared >= kMinSeconds))
            break;
        if (now)   // pressed again with too little: stop listening
        {
            const juce::ScopedLock sl (stateLock);
            state.stage = state.hasResult ? Stage::done : Stage::idle;
            return;
        }
        worker.wait (50);
    }

    {
        const juce::ScopedLock sl (stateLock);
        state.stage = Stage::analysing;
    }

    double fs = 48000.0;
    std::vector<float> ref, trk;
    juce::int64 start = 0;
    bool copied = false;
    for (int attempt = 0; attempt < 4 && ! copied; ++attempt)
        withCapture ([&] (const pf::Capture& c) {
            fs = c.sampleRate();
            const juce::int64 end = c.written();
            const int n = (int) std::min<juce::int64> (end, (juce::int64) (kWindowSeconds * fs));
            start = end - n;
            ref.resize ((size_t) n);
            trk.resize ((size_t) n);
            copied = c.copy (start, n, ref.data(), trk.data());
        });

    pf::AlignResult r;
    if (copied)
    {
        pf::AnalyzerSettings s;
        s.sampleRate = fs;
        s.minSeconds = kMinSeconds;
        r = pf::analyze (ref.data(), trk.data(), (int) ref.size(), s);
    }

    {
        const juce::ScopedLock sl (stateLock);
        pendingResult = r;
        pendingHit = r.hitSample >= 0 ? start + r.hitSample : -1;
        pendingOk = true;
    }
    triggerAsyncUpdate();
}

void PhaseFuckerProcessor::handleAsyncUpdate()
{
    pf::AlignResult r;
    juce::int64 hit;
    {
        const juce::ScopedLock sl (stateLock);
        if (! pendingOk) return;
        pendingOk = false;
        r = pendingResult;
        hit = pendingHit;
        state.stage = Stage::done;
        state.hasResult = true;
        state.result = r;
        state.hitSample = hit;
        state.finishedAt = juce::Time::getMillisecondCounter();
    }
    if (r.status == pf::AlignStatus::ok)
        applyResult (r);
}

void PhaseFuckerProcessor::setParam (const char* id, float plainValue)
{
    auto& p = param (id);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    p.endChangeGesture();
}

void PhaseFuckerProcessor::applyResult (const pf::AlignResult& r)
{
    const auto& c = r.correction;
    setParam (ids::compare, 0.f);
    setParam (ids::shift, (float) c.shiftMs);
    setParam (ids::invert, c.invert ? 1.f : 0.f);
    setParam (ids::rotate, c.rotate ? 1.f : 0.f);
    if (c.rotate)
    {
        setParam (ids::rotateFreq, (float) c.rotateHz);
        setParam (ids::rotateQ, (float) c.rotateQ);
    }
    updateLookahead();
}

void PhaseFuckerProcessor::resetCorrection()
{
    setParam (ids::shift, 0.f);
    setParam (ids::invert, 0.f);
    setParam (ids::rotate, 0.f);
    setParam (ids::compare, 0.f);
    updateLookahead();
}

// ---- Lookahead ----------------------------------------------------------------------

void PhaseFuckerProcessor::setLookaheadMode (int m)
{
    lookaheadMode = juce::jlimit (kAuto, (int) kLookaheadTiers.size() - 1, m);
    updateLookahead();
}

void PhaseFuckerProcessor::updateLookahead()
{
    shiftPending = false;
    double ms = 0.0;
    if (lookaheadMode == kAuto)
    {
        const double need = -shiftParam->load();
        ms = kLookaheadTiers.back();
        for (double t : kLookaheadTiers)
            if (t >= need - 1e-6) { ms = t; break; }
    }
    else
        ms = kLookaheadTiers[(size_t) lookaheadMode];

    if (! juce::exactlyEqual (ms, lookaheadMs.load()))
    {
        lookaheadMs = ms;
        setLatencySamples (pf::Aligner::latencyFor (ms, getRate()));
    }
}

void PhaseFuckerProcessor::parameterChanged (const juce::String&, float)
{
    // May come from the audio thread (automation): just note the time.
    shiftMovedAt = juce::Time::getMillisecondCounter();
    shiftPending = true;
}

void PhaseFuckerProcessor::timerCallback()
{
    if (shiftPending && lookaheadMode == kAuto && juce::Time::getMillisecondCounter() - shiftMovedAt.load() > 300)
        updateLookahead();
}

// ---- State -------------------------------------------------------------------------

void PhaseFuckerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty ("lookahead", lookaheadMode, nullptr);
    const auto st = getAlignState();
    if (st.hasResult)
    {
        juce::ValueTree res ("Result");
        res.setProperty ("status", (int) st.result.status, nullptr);
        res.setProperty ("shiftMs", st.result.correction.shiftMs, nullptr);
        res.setProperty ("invert", st.result.correction.invert, nullptr);
        res.setProperty ("rotate", st.result.correction.rotate, nullptr);
        res.setProperty ("rotateHz", st.result.correction.rotateHz, nullptr);
        res.setProperty ("rotateQ", st.result.correction.rotateQ, nullptr);
        res.setProperty ("confidence", st.result.confidence, nullptr);
        res.setProperty ("coherence", st.result.coherence, nullptr);
        res.setProperty ("lowEndGainDb", st.result.lowEndGainDb, nullptr);
        res.setProperty ("rotateGainDb", st.result.rotateGainDb, nullptr);
        tree.removeChild (tree.getChildWithName ("Result"), nullptr);
        tree.appendChild (res, nullptr);
    }
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaseFuckerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    lookaheadMode = juce::jlimit (kAuto, (int) kLookaheadTiers.size() - 1, (int) tree.getProperty ("lookahead", kAuto));
    const auto res = tree.getChildWithName ("Result");
    tree.removeChild (res, nullptr);
    apvts.replaceState (tree);

    if (res.isValid())
    {
        const juce::ScopedLock sl (stateLock);
        auto& r = state.result;
        r.status = (pf::AlignStatus) (int) res.getProperty ("status");
        r.correction.shiftMs = res.getProperty ("shiftMs");
        r.correction.invert = res.getProperty ("invert");
        r.correction.rotate = res.getProperty ("rotate");
        r.correction.rotateHz = res.getProperty ("rotateHz");
        r.correction.rotateQ = res.getProperty ("rotateQ");
        r.confidence = res.getProperty ("confidence");
        r.coherence = res.getProperty ("coherence");
        r.lowEndGainDb = res.getProperty ("lowEndGainDb");
        r.rotateGainDb = res.getProperty ("rotateGainDb");
        state.hasResult = true;
        state.stage = Stage::done;
        state.hitSample = -1;
        state.finishedAt = 0;
    }
    updateLookahead();
}

juce::AudioProcessorEditor* PhaseFuckerProcessor::createEditor() { return new PhaseFuckerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaseFuckerProcessor(); }
