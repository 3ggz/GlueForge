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
    bypassParam     = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gf::params::id::bypass));
    jassert (gainParam != nullptr && bypassParam != nullptr && thresholdParam != nullptr
             && mixParam != nullptr && rangeParam != nullptr && linkParam != nullptr);
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

    scFilter.prepare (sampleRate, numOut);
    ducker.prepare (sampleRate);
    saturator.prepare (sampleRate, numOut);
    compressor.prepare (sampleRate, numOut);
    cpValid = false; // force coefficient recompute for the new sample rate on next block

    // No lookahead/oversampling yet — but the PDC hook is live from day one.
    setLatencySamples (0);
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

    // Keep a dry copy for the parallel (wet/dry) mix.
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus.getReadPointer (ch), numSamples);

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
            lastCp  = cp;
            cpValid = true;
        }
        compressor.process (mainBus, &detectionBuffer);
        grMeterDb.store (compressor.getGainReductionDb());
    }

    // Character saturation colours the wet (processed) signal before the mix.
    saturator.setModel (charModel);
    saturator.setDrive (driveParam->load());
    saturator.setMix   (satMixParam->load());
    saturator.process (mainBus);

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
