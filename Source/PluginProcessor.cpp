#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

namespace
{
    float blockPeakDb (const juce::AudioBuffer<float>& buf, int numCh, int numSamples)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            peak = juce::jmax (peak, buf.getMagnitude (ch, 0, numSamples));
        return juce::Decibels::gainToDecibels (peak, -100.0f);
    }

    // In-place L/R <-> M/S (round-trips exactly). Stereo only.
    void msEncode (juce::AudioBuffer<float>& buf, int numCh, int n)
    {
        if (numCh < 2) return;
        auto* l = buf.getWritePointer (0); auto* r = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float L = l[i], R = r[i]; l[i] = 0.5f * (L + R); r[i] = 0.5f * (L - R); }
    }
    void msDecode (juce::AudioBuffer<float>& buf, int numCh, int n)
    {
        if (numCh < 2) return;
        auto* m = buf.getWritePointer (0); auto* s = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float M = m[i], S = s[i]; m[i] = M + S; s[i] = M - S; }
    }
}

GlueForgeProcessor::GlueForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", gf::params::createParameterLayout())
{
    gainParam      = apvts.getRawParameterValue (gf::params::id::gain);
    thresholdParam = apvts.getRawParameterValue (gf::params::id::threshold);
    ratioParam     = apvts.getRawParameterValue (gf::params::id::ratio);
    kneeParam      = apvts.getRawParameterValue (gf::params::id::knee);
    attackParam    = apvts.getRawParameterValue (gf::params::id::attack);
    releaseParam   = apvts.getRawParameterValue (gf::params::id::release);
    holdParam      = apvts.getRawParameterValue (gf::params::id::hold);
    makeupParam     = apvts.getRawParameterValue (gf::params::id::makeup);
    detectorParam   = apvts.getRawParameterValue (gf::params::id::detector);
    mixParam        = apvts.getRawParameterValue (gf::params::id::mix);
    rangeParam      = apvts.getRawParameterValue (gf::params::id::range);
    linkParam       = apvts.getRawParameterValue (gf::params::id::link);
    autoMakeupParam = apvts.getRawParameterValue (gf::params::id::automakeup);
    triggerParam    = apvts.getRawParameterValue (gf::params::id::trigger);
    scHpfParam      = apvts.getRawParameterValue (gf::params::id::scHpf);
    scLpfParam      = apvts.getRawParameterValue (gf::params::id::scLpf);
    scListenParam   = apvts.getRawParameterValue (gf::params::id::scListen);
    duckRateParam   = apvts.getRawParameterValue (gf::params::id::duckRate);
    duckDepthParam  = apvts.getRawParameterValue (gf::params::id::duckDepth);
    duckCurveParam  = apvts.getRawParameterValue (gf::params::id::duckCurve);
    syncReleaseParam = apvts.getRawParameterValue (gf::params::id::syncRelease);
    releaseDivParam  = apvts.getRawParameterValue (gf::params::id::releaseDiv);
    characterParam  = apvts.getRawParameterValue (gf::params::id::character);
    driveParam      = apvts.getRawParameterValue (gf::params::id::drive);
    satMixParam     = apvts.getRawParameterValue (gf::params::id::satMix);
    lookaheadParam    = apvts.getRawParameterValue (gf::params::id::lookahead);
    oversamplingParam = apvts.getRawParameterValue (gf::params::id::oversampling);
    midsideParam   = apvts.getRawParameterValue (gf::params::id::midside);
    mbEnableParam  = apvts.getRawParameterValue (gf::params::id::mbEnable);
    mbXLowParam    = apvts.getRawParameterValue (gf::params::id::mbXLow);
    mbXHighParam   = apvts.getRawParameterValue (gf::params::id::mbXHigh);
    mbTrimParam[0] = apvts.getRawParameterValue (gf::params::id::mbTrim1);
    mbTrimParam[1] = apvts.getRawParameterValue (gf::params::id::mbTrim2);
    mbTrimParam[2] = apvts.getRawParameterValue (gf::params::id::mbTrim3);
    mbBypassParam[0] = apvts.getRawParameterValue (gf::params::id::mbBypass1);
    mbBypassParam[1] = apvts.getRawParameterValue (gf::params::id::mbBypass2);
    mbBypassParam[2] = apvts.getRawParameterValue (gf::params::id::mbBypass3);
    mbSoloParam    = apvts.getRawParameterValue (gf::params::id::mbSolo);
    bypassParam     = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gf::params::id::bypass));
    jassert (gainParam != nullptr && bypassParam != nullptr && thresholdParam != nullptr
             && mixParam != nullptr && rangeParam != nullptr && linkParam != nullptr);

    // Latency changes (lookahead / oversampling) recompute PDC off the audio thread.
    apvts.addParameterListener (gf::params::id::lookahead, this);
    apvts.addParameterListener (gf::params::id::oversampling, this);
}

GlueForgeProcessor::~GlueForgeProcessor()
{
    apvts.removeParameterListener (gf::params::id::lookahead, this);
    apvts.removeParameterListener (gf::params::id::oversampling, this);
    cancelPendingUpdate();
}

void GlueForgeProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate(); // recompute latency on the message thread
}

void GlueForgeProcessor::handleAsyncUpdate()
{
    updateLatency();
}

void GlueForgeProcessor::updateLatency()
{
    const int la = (int) std::lround (lookaheadParam->load() * currentSr / 1000.0);
    int os = 0;
    const int idx = (int) oversamplingParam->load();
    if (idx > 0 && oversamplers[(size_t) (idx - 1)] != nullptr)
        os = (int) oversamplers[(size_t) (idx - 1)]->getLatencyInSamples();
    setLatencySamples (la + os);
}

void GlueForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numOut = juce::jmax (1, getMainBusNumOutputChannels());

    gainSmoothed.reset (sampleRate, 0.02); // 20 ms ramp — click-free
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));

    mixSmoothed.reset (sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue (mixParam->load());

    dryBuffer.setSize       (numOut, samplesPerBlock, false, false, true); // pre-allocate dry copy
    detectionBuffer.setSize (numOut, samplesPerBlock, false, false, true); // key/detection signal

    currentSr = sampleRate;

    scFilter.prepare (sampleRate, numOut);
    ducker.prepare (sampleRate);
    saturator.prepare (sampleRate, numOut);
    compressor.prepare (sampleRate, numOut);
    multiband.prepare (sampleRate, numOut, samplesPerBlock);
    cpValid = false; // force coefficient recompute for the new sample rate on next block

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) numOut };
    lookaheadDelay.prepare (spec);
    lookaheadDelay.reset();

    for (int i = 0; i < 3; ++i) // 2x / 4x / 8x
    {
        oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numOut, (size_t) (i + 1),
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);
        oversamplers[(size_t) i]->initProcessing ((size_t) samplesPerBlock);
        oversamplers[(size_t) i]->reset();
    }

    updateLatency();
}

bool GlueForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();

    // Main output must be mono or stereo.
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Main input must match the main output.
    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    // Optional sidechain input (bus 1): disabled, mono, or stereo only.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void GlueForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, false, 0); // view into the main output channels
    const int numCh      = mainBus.getNumChannels();
    const int numSamples = mainBus.getNumSamples();

    inLevelDb.store (blockPeakDb (mainBus, numCh, numSamples));

    if (bypassParam->get())
    {
        // Bypassed: audio passes through untouched. Keep state in sync so
        // re-engaging does not jump.
        gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
        mixSmoothed.setCurrentAndTargetValue (mixParam->load());
        grMeterDb.store (0.0f);
        outLevelDb.store (inLevelDb.load());
        return;
    }

    // Host transport for tempo-sync; falls back to 120 BPM when unavailable.
    double bpm = 120.0, ppq = 0.0;
    bool havePos = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())         bpm = *b;
            if (auto q = pos->getPpqPosition()) { ppq = *q; havePos = true; }
        }

    // --- Build the detection (key) signal: external sidechain if selected and
    //     actually connected, otherwise the main signal; then the SC filter. ---
    const int trigger = (int) triggerParam->load();

    bool scConnected = false;
    if (auto* scBus = getBus (true, 1))
        scConnected = scBus->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    const bool useExternal = (trigger == 1) && scConnected;

    if (useExternal)
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        const int scCh = juce::jmax (1, scBuf.getNumChannels());
        for (int ch = 0; ch < numCh; ++ch)
            detectionBuffer.copyFrom (ch, 0, scBuf.getReadPointer (juce::jmin (ch, scCh - 1)), numSamples);
    }
    else
    {
        for (int ch = 0; ch < numCh; ++ch)
            detectionBuffer.copyFrom (ch, 0, mainBus.getReadPointer (ch), numSamples);
    }

    scFilter.setCutoffs (scHpfParam->load(), scLpfParam->load());
    if (scFilter.isActive())
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = detectionBuffer.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
                d[n] = scFilter.processSample (ch, d[n]);
        }

    // Sidechain audition: output the (filtered) key signal, skip compression.
    if (scListenParam->load() >= 0.5f)
    {
        gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
        mixSmoothed.setCurrentAndTargetValue (mixParam->load());
        auto* const* w = mainBus.getArrayOfWritePointers();
        auto* const* d = detectionBuffer.getArrayOfReadPointers();
        for (int n = 0; n < numSamples; ++n)
        {
            const float og = gainSmoothed.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
                w[ch][n] = d[ch][n] * og;
        }
        grMeterDb.store (0.0f);
        outLevelDb.store (blockPeakDb (mainBus, numCh, numSamples));
        return;
    }

    // Lookahead: delay the audio so the (undelayed) detector reacts ahead of it.
    const int laSamples = (int) juce::jlimit (0.0, 8000.0,
                                              std::round (lookaheadParam->load() * currentSr / 1000.0));
    if (laSamples > 0)
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = mainBus.getWritePointer (ch);
            for (int n = 0; n < numSamples; ++n)
            {
                lookaheadDelay.pushSample (ch, d[n]);
                d[n] = lookaheadDelay.popSample (ch, (float) laSamples);
            }
        }

    // Keep a dry copy (post-delay, so dry/wet stay phase-aligned for the mix).
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus.getReadPointer (ch), numSamples);

    // Mid/Side: compress in the M/S domain (stereo only). Dry stays L/R.
    const bool midSide = midsideParam->load() >= 0.5f && numCh >= 2;
    if (midSide)
    {
        msEncode (mainBus, numCh, numSamples);
        msEncode (detectionBuffer, numCh, numSamples);
    }

    const auto charModel = (gf::dsp::CharacterModel) juce::jlimit (0, 2, (int) characterParam->load());

    // 1) Wet generation: tempo-synced volume-shaper duck, or the compressor.
    if (trigger == 2) // Tempo Duck
    {
        ducker.setParameters (duckDepthParam->load(),
                              juce::jmap (duckCurveParam->load(), 0.0f, 1.0f, 0.3f, 4.0f));
        const double divBeats = gf::dsp::divisionBeats ((int) duckRateParam->load());
        ducker.setRate (bpm, divBeats);
        if (havePos)
            ducker.syncToPpq (ppq, divBeats); // re-lock to the host transport each block

        auto* const* w = mainBus.getArrayOfWritePointers();
        float worst = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float g = ducker.processSample();
            for (int ch = 0; ch < numCh; ++ch)
                w[ch][n] *= g;
            worst = juce::jmin (worst, juce::Decibels::gainToDecibels (g, -100.0f));
        }
        grMeterDb.store (worst);
    }
    else // Internal / External SC -> the compressor
    {
        gf::dsp::CompressorParameters cp;
        cp.thresholdDb   = thresholdParam->load();
        cp.ratio         = ratioParam->load();
        cp.kneeDb        = kneeParam->load();
        // Character model scales attack/release (FET snappier, Opto smoother).
        cp.attackMs      = attackParam->load() * gf::dsp::characterAttackScale (charModel);
        // Synced release depends on BPM (external to the param set) — compute it
        // into cp.releaseMs *before* the change check so a tempo change re-applies.
        cp.releaseMs     = ((syncReleaseParam->load() >= 0.5f)
                              ? (float) gf::dsp::beatsToMs (gf::dsp::divisionBeats ((int) releaseDivParam->load()), bpm)
                              : releaseParam->load())
                           * gf::dsp::characterReleaseScale (charModel);
        cp.holdMs        = holdParam->load();
        cp.makeupDb      = makeupParam->load();
        cp.detectorBlend = detectorParam->load();
        cp.rangeDb       = rangeParam->load();
        cp.stereoLink    = linkParam->load();
        cp.autoMakeup    = autoMakeupParam->load() >= 0.5f;
        if (! cpValid || cp != lastCp)    // only recompute coefficients when something changed
        {
            compressor.setParameters (cp);
            multiband.setCompressorParams (cp);
            lastCp  = cp;
            cpValid = true;
        }

        if (mbEnableParam->load() >= 0.5f)
        {
            multiband.setCrossovers (mbXLowParam->load(), mbXHighParam->load());
            for (int b = 0; b < 3; ++b)
                multiband.setBand (b, mbTrimParam[b]->load(), mbBypassParam[b]->load() >= 0.5f);
            multiband.setSolo ((int) mbSoloParam->load() - 1); // 0 None -> -1
            multiband.process (mainBus, detectionBuffer);
            grMeterDb.store (multiband.getGainReductionDb());
        }
        else
        {
            compressor.process (mainBus, &detectionBuffer);
            grMeterDb.store (compressor.getGainReductionDb());
        }
    }

    if (midSide)
        msDecode (mainBus, numCh, numSamples); // back to L/R

    // Character saturation colours the wet signal — oversampled (around the
    // nonlinearity) when enabled, to keep the harmonics from aliasing.
    saturator.setModel (charModel);
    saturator.setDrive (driveParam->load());
    saturator.setMix   (satMixParam->load());
    const int osIdx = (int) oversamplingParam->load();
    if (osIdx > 0 && oversamplers[(size_t) (osIdx - 1)] != nullptr)
    {
        juce::dsp::AudioBlock<float> baseBlock (mainBus);
        auto osBlock = oversamplers[(size_t) (osIdx - 1)]->processSamplesUp (baseBlock);
        saturator.process (osBlock);
        oversamplers[(size_t) (osIdx - 1)]->processSamplesDown (baseBlock);
    }
    else
    {
        saturator.process (mainBus);
    }

    // 2) Parallel mix (wet/dry) + output gain in one smoothed per-sample pass.
    mixSmoothed.setTargetValue (mixParam->load());
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));

    auto* const* wet = mainBus.getArrayOfWritePointers();
    auto* const* dry = dryBuffer.getArrayOfReadPointers();
    for (int n = 0; n < numSamples; ++n)
    {
        const float m  = mixSmoothed.getNextValue();
        const float og = gainSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            wet[ch][n] = (wet[ch][n] * m + dry[ch][n] * (1.0f - m)) * og;
    }

    outLevelDb.store (blockPeakDb (mainBus, numCh, numSamples));
}

void GlueForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GlueForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* GlueForgeProcessor::createEditor()
{
    return new GlueForgeEditor (*this);
}

// This creates the plugin instance for every format (VST3, AU, Standalone).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlueForgeProcessor();
}
