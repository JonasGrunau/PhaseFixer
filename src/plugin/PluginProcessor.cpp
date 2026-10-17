#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <limits>

namespace
{
    // "bar 5", or "bar 5 beat 3" off the downbeat, counted the way hosts show it.
    juce::String barText (double ppq, double barPpq, int numerator, int denominator)
    {
        const double barLength = numerator * 4.0 / denominator, beat = 4.0 / denominator;
        const int bar = (int) std::lround (barPpq / barLength) + 1;
        const double into = ppq - barPpq;
        if (into < 0.02 * beat) return "bar " + juce::String (bar);
        return "bar " + juce::String (bar) + " beat " + juce::String ((int) std::floor (into / beat + 0.02) + 1);
    }
} // namespace

PhaseFuckerProcessor::PhaseFuckerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Reference", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PhaseFucker", createLayout())
{
    phaseParam = apvts.getRawParameterValue (ids::phase);
    invertParam = apvts.getRawParameterValue (ids::invert);
    compareParam = apvts.getRawParameterValue (ids::compare);
    perHitParam = apvts.getRawParameterValue (ids::perHit);

    capture.prepare (48000.0);
    rotator.prepare (48000.0, 2);
    setLatencySamples (rotator.latency());
    worker.startThread (juce::Thread::Priority::low);
}

PhaseFuckerProcessor::~PhaseFuckerProcessor()
{
    worker.signalThreadShouldExit();
    worker.notify();
    worker.stopThread (4000);
}

juce::AudioProcessorValueTreeState::ParameterLayout PhaseFuckerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::phase, 2 }, "Phase",
        NormalisableRange<float> (-(float) pf::limits::kMaxPhaseDeg, (float) pf::limits::kMaxPhaseDeg), 0.f,
        AudioParameterFloatAttributes().withLabel (CharPointer_UTF8 ("\xc2\xb0"))
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + String (CharPointer_UTF8 ("\xc2\xb0")); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::invert, 1 }, "Polarity", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::compare, 1 }, "Compare", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::perHit, 1 }, "Per hit", false,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));
    return layout;
}

pf::Correction PhaseFuckerProcessor::correction() const noexcept
{
    pf::Correction c;
    c.phaseDeg = phaseParam->load (std::memory_order_relaxed);
    c.invert = invertParam->load (std::memory_order_relaxed) > 0.5f;
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
    rotator.prepare (sampleRate, std::max (1, getMainBusNumInputChannels()));
    mono.setSize (2, std::max (1, samplesPerBlock));
    setLatencySamples (rotator.latency());
}

void PhaseFuckerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, true, 0);
    const auto* sideBus = getBus (true, 1);
    const bool hasSide = sideBus != nullptr && sideBus->isEnabled() && sideBus->getNumberOfChannels() > 0;
    sidechainActive.store (hasSide, std::memory_order_relaxed);
    if (restartRequested.exchange (false))
        capture.restart();
    const auto now = readTransport (capture.written());
    capture.setTimeline (now.valid ? now.ppq : std::numeric_limits<double>::quiet_NaN(), now.bpm / 60.0 / getRate());

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

    const double turn = turnFor (now, n);
    liveTurn.store (turn, std::memory_order_relaxed);
    recordForExport (main, now, turn);
    rotator.setTarget (turn, isComparing());
    rotator.process (main.getArrayOfWritePointers(), main.getNumChannels(), n);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);
}

double PhaseFuckerProcessor::turnFor (const Transport& t, int numSamples) noexcept
{
    const double knob = correction().totalDeg();
    if (! isPerHit())
    {
        liveMapIndex.store (-1, std::memory_order_relaxed);
        return knob;
    }
    // While the transport stands (or the map is being edited) the last turn holds.
    if (t.valid)
        if (const juce::SpinLock::ScopedTryLockType l (mapLock); l.isLocked())
        {
            if (map.empty())
            {
                mapTurn = 0.0;
                liveMapIndex.store (-1, std::memory_order_relaxed);
            }
            else
            {
                // This block comes out `latency` samples late, and the rotator
                // takes a glide to get anywhere: aim where it will have arrived.
                const double fs = getRate();
                const double ahead = std::min (numSamples, (int) (pf::Rotator::kFadeMs * 0.001 * fs)) - rotator.latency();
                const double q = t.ppq + ahead * t.bpm / 60.0 / fs;
                mapTurn = map.angleAt (q, t.bpm);
                liveMapIndex.store (map.indexAt (q), std::memory_order_relaxed);
            }
        }
    return pf::wrapDeg (mapTurn + knob);
}

PhaseFuckerProcessor::Transport PhaseFuckerProcessor::readTransport (juce::int64 index) noexcept
{
    Transport t;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (pos->getIsPlaying() && pos->getPpqPosition().hasValue() && pos->getBpm().hasValue() && *pos->getBpm() > 1.0)
            {
                t.valid = true;
                t.index = index;
                t.ppq = *pos->getPpqPosition();
                t.bpm = *pos->getBpm();
                if (const auto sig = pos->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
                {
                    t.numerator = sig->numerator;
                    t.denominator = sig->denominator;
                }
                const double barLength = t.numerator * 4.0 / t.denominator;
                t.barPpq = pos->getPpqPositionOfLastBarStart().orFallback (std::floor (t.ppq / barLength) * barLength);
                if (const auto loop = pos->getLoopPoints(); pos->getIsLooping() && loop.hasValue() && loop->ppqEnd > loop->ppqStart)
                {
                    t.looping = true;
                    t.loopStart = loop->ppqStart;
                    t.loopEnd = loop->ppqEnd;
                }
            }

    // A jump (loop, locate, restart of the capture) starts a new stretch.
    if (t.valid)
    {
        const double expected = lastPpq + (double) (index - lastIndex) * lastBpm / 60.0 / getRate();
        if (! lastValid || index < lastIndex || std::abs (t.ppq - expected) > 0.01)
            contiguousFrom = index;
        t.contiguousFrom = contiguousFrom;
        lastPpq = t.ppq;
        lastBpm = t.bpm;
        lastIndex = index;
    }
    lastValid = t.valid;

    const juce::SpinLock::ScopedTryLockType l (transportLock);
    if (l.isLocked())
        transport = t;
    return t;
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
        wait (100);   // the audio thread doesn't notify: an export is picked up within 100 ms
        if (threadShouldExit()) return;
        if (owner.jobRequested.exchange (false))
            owner.runAlignJob();
        if (owner.exportStage.load (std::memory_order_acquire) == (int) ExportStage::rendering)
            owner.renderExport();
    }
}

void PhaseFuckerProcessor::runAlignJob()
{
    const bool perHit = isPerHit();
    // Listen until there is enough shared signal (or ALIGN is pressed again).
    for (;;)
    {
        if (worker.threadShouldExit()) return;
        pf::Capture::Activity act;
        withCapture ([&] (const pf::Capture& c) { act = c.activity (kWindowSeconds); });
        // How long each has played, not how long both at once: a kick and an
        // offbeat bass take turns and hardly ever sound together.
        const double heard = std::min (act.ref, act.track);
        const bool now = analyseNow.load();
        {
            const juce::ScopedLock sl (stateLock);
            state.progress = (float) juce::jlimit (0.0, 1.0, heard / kNeededSeconds);
        }
        if (heard >= kNeededSeconds || (now && heard >= kMinSeconds))
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

    // Per hit: place the hits on the timeline, and note which stretch of it
    // was heard (a loop covers the same stretch more than once).
    std::vector<pf::PhaseMap::Hit> hits;
    double from = std::numeric_limits<double>::max(), to = std::numeric_limits<double>::lowest();
    if (perHit && copied)
        withCapture ([&] (const pf::Capture& c) {
            for (juce::int64 i = start; i < start + (juce::int64) ref.size(); i += pf::Capture::kBlock)
                if (const double p = c.timelineAt (i); std::isfinite (p))
                {
                    from = std::min (from, p);
                    to = std::max (to, p);
                }
            for (const auto& h : r.hits)
                if (const double p = c.timelineAt (start + h.sample); std::isfinite (p))
                    hits.push_back ({ p, h.totalDeg, h.gainDb, h.fit });
        });

    {
        const juce::ScopedLock sl (stateLock);
        pendingResult = r;
        pendingHit = r.hitSample >= 0 ? start + r.hitSample : -1;
        pendingOk = true;
        pendingPerHit = perHit;
        pendingHits = std::move (hits);
        pendingFrom = from;
        pendingTo = to;
        pendingBpm = getTransport().bpm;
    }
    triggerAsyncUpdate();
}

void PhaseFuckerProcessor::handleAsyncUpdate()
{
    pf::AlignResult r;
    bool perHit = false;
    {
        const juce::ScopedLock sl (stateLock);
        if (! pendingOk) return;
        pendingOk = false;
        r = pendingResult;
        perHit = pendingPerHit;
        state.stage = Stage::done;
        state.hasResult = true;
        state.result = r;
        state.hitSample = pendingHit;
        state.finishedAt = juce::Time::getMillisecondCounter();
        state.perHit = perHit;
        state.mappedHits = 0;
        state.noTimeline = perHit && pendingFrom > pendingTo;
    }
    if (r.status != pf::AlignStatus::ok) return;
    if (perHit) learnMap();
    else applyResult (r);
}

void PhaseFuckerProcessor::learnMap()
{
    std::vector<pf::PhaseMap::Hit> hits;
    double from, to, bpm;
    {
        const juce::ScopedLock sl (stateLock);
        hits = std::move (pendingHits);
        from = pendingFrom;
        to = pendingTo;
        bpm = pendingBpm;
        state.mappedHits = (int) hits.size();
    }
    if (hits.empty()) return;
    {
        const juce::SpinLock::ScopedLockType l (mapLock);
        map.learn (std::move (hits), from, to, bpm);
    }
    ++mapRevision;
    // The knob is an offset on top of the map now; start it from zero.
    setParam (ids::compare, 0.f);
    setParam (ids::phase, 0.f);
    setParam (ids::invert, 0.f);
}

pf::PhaseMap PhaseFuckerProcessor::getMap() const
{
    const juce::SpinLock::ScopedLockType l (mapLock);
    return map;
}

void PhaseFuckerProcessor::editMapPoint (int index, double deg)
{
    {
        const juce::SpinLock::ScopedLockType l (mapLock);
        map.edit (index, deg);
    }
    ++mapRevision;
}

void PhaseFuckerProcessor::clearMap()
{
    {
        const juce::SpinLock::ScopedLockType l (mapLock);
        map.points.clear();
    }
    ++mapRevision;
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
    setParam (ids::compare, 0.f);
    setParam (ids::phase, (float) r.correction.phaseDeg);
    setParam (ids::invert, r.correction.invert ? 1.f : 0.f);
}

// ---- Export ------------------------------------------------------------------------

void PhaseFuckerProcessor::toggleExport()
{
    auto stage = exportStage.load (std::memory_order_acquire);
    if (stage == (int) ExportStage::armed && exportStage.compare_exchange_strong (stage, (int) ExportStage::idle))
        return;
    if (stage == (int) ExportStage::recording || stage == (int) ExportStage::armed)   // started meanwhile
    {
        exportStopRequested = true;
        return;
    }
    if (stage == (int) ExportStage::rendering) return;

    // Idle, ready or failed: the audio thread leaves the buffers alone.
    const int channels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    const int samples = (int) (kExportSeconds * getRate());
    if (recBuffer.getNumChannels() != channels || recBuffer.getNumSamples() != samples)
        recBuffer.setSize (channels, samples);
    recTurns.clear();
    recTurns.reserve (1 << 16);
    exportStopRequested = false;
    exportStage.store ((int) ExportStage::armed, std::memory_order_release);
}

void PhaseFuckerProcessor::recordForExport (const juce::AudioBuffer<float>& in, const Transport& t, double turn) noexcept
{
    auto stage = exportStage.load (std::memory_order_acquire);
    if (stage == (int) ExportStage::armed)
    {
        // Starts with playback, or when the loop comes round.
        if (! t.valid || t.contiguousFrom != t.index) return;
        if (! exportStage.compare_exchange_strong (stage, (int) ExportStage::recording)) return;
        stage = (int) ExportStage::recording;
        recLength.store (0, std::memory_order_relaxed);
        recTurns.clear();
        recChannels = juce::jmin (in.getNumChannels(), recBuffer.getNumChannels());
        recStartPpq = t.ppq;
        recBpm = t.bpm;
        recBarPpq = t.barPpq;
        recNumerator = t.numerator;
        recDenominator = t.denominator;
        recLoopEnd = t.looping && t.loopStart <= t.ppq + 1e-3 ? t.loopEnd : -1.0;
        recLooped = false;
    }
    if (stage != (int) ExportStage::recording) return;

    const int length = recLength.load (std::memory_order_relaxed);
    const bool cameRound = t.valid && t.contiguousFrom == t.index && length > 0;
    if (! t.valid || cameRound || exportStopRequested.exchange (false) || length >= recBuffer.getNumSamples())
    {
        recLooped = cameRound && std::abs (t.ppq - recStartPpq) < 1e-3;
        exportStage.store ((int) ExportStage::rendering, std::memory_order_release);
        return;
    }

    if ((recTurns.empty() || ! juce::exactlyEqual (recTurns.back().deg, turn)) && recTurns.size() < recTurns.capacity())
        recTurns.push_back ({ length, turn });
    const int n = juce::jmin (in.getNumSamples(), recBuffer.getNumSamples() - length);
    for (int c = 0; c < recChannels; ++c)
        recBuffer.copyFrom (c, length, in, c, 0, n);
    recLength.store (length + n, std::memory_order_release);
}

void PhaseFuckerProcessor::renderExport()
{
    const double fs = getRate();
    int n = recLength.load (std::memory_order_acquire);
    // A loop pass is exactly the loop, whatever block the host wrapped in.
    if (recLooped && recLoopEnd > recStartPpq)
        n = juce::jmin (n, (int) std::lround ((recLoopEnd - recStartPpq) * 60.0 / recBpm * fs));

    juce::File file;
    const auto where = barText (recStartPpq, recBarPpq, recNumerator, recDenominator);
    if (n >= (int) (0.1 * fs))
    {
        juce::AudioBuffer<float> out (recChannels, n);
        pf::renderOffline (recBuffer.getArrayOfReadPointers(), out.getArrayOfWritePointers(), recChannels, n, fs, recTurns, recLooped);

        juce::String name;
        {
            const juce::ScopedLock sl (stateLock);
            name = trackName;
        }
        const auto dir = exportDirectory;
        dir.createDirectory();
        const auto title = (name.isEmpty() ? juce::String ("PhaseFucker") : name) + " - " + where + " - "
                         + theme::fixed (recBpm, juce::exactlyEqual (recBpm, std::round (recBpm)) ? 0 : 2) + " BPM";
        file = dir.getNonexistentChildFile (juce::File::createLegalFileName (title), ".wav", false);

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream&> (*stream).openedOk())
            file = juce::File();
        else if (auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                                                   .withSampleRate (fs)
                                                                                   .withNumChannels (recChannels)
                                                                                   .withBitsPerSample (24)))
        {
            if (! writer->writeFromAudioSampleBuffer (out, 0, n)) file = juce::File();
        }
        else
            file = juce::File();
    }

    {
        const juce::ScopedLock sl (stateLock);
        exportFile = file;
        exportWhere = where;
        exportSeconds = n / fs;
    }
    exportStage.store ((int) (file == juce::File() ? ExportStage::failed : ExportStage::ready), std::memory_order_release);
}

PhaseFuckerProcessor::ExportInfo PhaseFuckerProcessor::getExportInfo() const
{
    ExportInfo e;
    e.stage = (ExportStage) exportStage.load (std::memory_order_acquire);
    const juce::ScopedLock sl (stateLock);
    if (e.stage == ExportStage::recording)
    {
        e.seconds = recLength.load (std::memory_order_relaxed) / getRate();
        e.where = barText (recStartPpq, recBarPpq, recNumerator, recDenominator);
    }
    else if (e.stage == ExportStage::ready || e.stage == ExportStage::failed)
    {
        e.seconds = exportSeconds;
        e.where = exportWhere;
        e.file = exportFile;
    }
    return e;
}

void PhaseFuckerProcessor::updateTrackProperties (const TrackProperties& p)
{
    const juce::ScopedLock sl (stateLock);
    trackName = p.name.value_or (juce::String());
}

void PhaseFuckerProcessor::resetCorrection()
{
    setParam (ids::phase, 0.f);
    setParam (ids::invert, 0.f);
    setParam (ids::compare, 0.f);
}

// ---- State -------------------------------------------------------------------------

void PhaseFuckerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty ("scopeTrigger", (int) scopeTrigger, nullptr);
    tree.setProperty ("scopeZoom", scopeZoomMs, nullptr);
    tree.setProperty ("scopeBars", scopeBars, nullptr);
    const auto st = getAlignState();
    if (st.hasResult)
    {
        juce::ValueTree res ("Result");
        res.setProperty ("status", (int) st.result.status, nullptr);
        res.setProperty ("phaseDeg", st.result.correction.phaseDeg, nullptr);
        res.setProperty ("invert", st.result.correction.invert, nullptr);
        res.setProperty ("confidence", st.result.confidence, nullptr);
        res.setProperty ("coherence", st.result.coherence, nullptr);
        res.setProperty ("lowEndGainDb", st.result.lowEndGainDb, nullptr);
        res.setProperty ("sumGainDb", st.result.sumGainDb, nullptr);
        res.setProperty ("hasOffset", st.result.hasOffset, nullptr);
        res.setProperty ("offsetSamples", st.result.offsetSamples, nullptr);
        res.setProperty ("timeFixDb", st.result.timeFixDb, nullptr);
        tree.removeChild (tree.getChildWithName ("Result"), nullptr);
        tree.appendChild (res, nullptr);
    }
    {
        const juce::SpinLock::ScopedLockType l (mapLock);
        tree.removeChild (tree.getChildWithName ("Map"), nullptr);
        if (! map.empty())
        {
            juce::ValueTree m ("Map");
            for (const auto& p : map.points)
            {
                juce::ValueTree pt ("Point");
                pt.setProperty ("ppq", p.ppq, nullptr);
                pt.setProperty ("deg", p.deg, nullptr);
                pt.setProperty ("learnedDeg", p.learnedDeg, nullptr);
                pt.setProperty ("gainDb", p.gainDb, nullptr);
                pt.setProperty ("fit", p.fit, nullptr);
                pt.setProperty ("edited", p.edited, nullptr);
                m.appendChild (pt, nullptr);
            }
            tree.appendChild (m, nullptr);
        }
    }
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PhaseFuckerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    scopeTrigger = (Trigger) juce::jlimit (0, 2, (int) tree.getProperty ("scopeTrigger", (int) Trigger::bar));
    scopeZoomMs = (float) tree.getProperty ("scopeZoom", 1000.f);
    scopeBars = juce::jlimit (0.f, 4.f, (float) tree.getProperty ("scopeBars", 0.5f));
    const auto res = tree.getChildWithName ("Result");
    tree.removeChild (res, nullptr);
    const auto saved = tree.getChildWithName ("Map");
    tree.removeChild (saved, nullptr);
    apvts.replaceState (tree);

    {
        pf::PhaseMap m;
        for (const auto& pt : saved)
        {
            pf::MapPoint p;
            p.ppq = pt.getProperty ("ppq");
            p.deg = pt.getProperty ("deg");
            p.learnedDeg = pt.getProperty ("learnedDeg");
            p.gainDb = pt.getProperty ("gainDb");
            p.fit = pt.getProperty ("fit");
            p.edited = pt.getProperty ("edited");
            m.points.push_back (p);
        }
        std::sort (m.points.begin(), m.points.end(), [] (const auto& a, const auto& b) { return a.ppq < b.ppq; });
        const juce::SpinLock::ScopedLockType l (mapLock);
        map = std::move (m);
    }
    ++mapRevision;

    if (res.isValid())
    {
        const juce::ScopedLock sl (stateLock);
        auto& r = state.result;
        r.status = (pf::AlignStatus) (int) res.getProperty ("status");
        r.correction.phaseDeg = res.getProperty ("phaseDeg");
        r.correction.invert = res.getProperty ("invert");
        r.confidence = res.getProperty ("confidence");
        r.coherence = res.getProperty ("coherence");
        r.lowEndGainDb = res.getProperty ("lowEndGainDb");
        r.sumGainDb = res.getProperty ("sumGainDb");
        r.hasOffset = res.getProperty ("hasOffset");
        r.offsetSamples = res.getProperty ("offsetSamples");
        r.timeFixDb = res.getProperty ("timeFixDb");
        state.hasResult = true;
        state.stage = Stage::done;
        state.hitSample = -1;
        state.finishedAt = 0;
    }
}

juce::AudioProcessorEditor* PhaseFuckerProcessor::createEditor() { return new PhaseFuckerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PhaseFuckerProcessor(); }
