#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

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

    dryBuffer.setSize (numOut, samplesPerBlock, false, false, true); // pre-allocate dry copy

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

    if (bypassParam->get())
    {
        // Bypassed: audio passes through untouched. Keep state in sync so
        // re-engaging does not jump.
        gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
        mixSmoothed.setCurrentAndTargetValue (mixParam->load());
        grMeterDb.store (0.0f);
        return;
    }

    // Keep a dry copy for the parallel (wet/dry) mix.
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, mainBus.getReadPointer (ch), numSamples);

    // 1) Compression (detector -> static curve -> range -> ballistics -> gain + makeup).
    gf::dsp::CompressorParameters cp;
    cp.thresholdDb   = thresholdParam->load();
    cp.ratio         = ratioParam->load();
    cp.kneeDb        = kneeParam->load();
    cp.attackMs      = attackParam->load();
    cp.releaseMs     = releaseParam->load();
    cp.holdMs        = holdParam->load();
    cp.makeupDb      = makeupParam->load();
    cp.detectorBlend = detectorParam->load();
    cp.rangeDb       = rangeParam->load();
    cp.stereoLink    = linkParam->load();
    cp.autoMakeup    = autoMakeupParam->load() >= 0.5f;
    if (! cpValid || cp != lastCp)        // only recompute coefficients when something changed
    {
        compressor.setParameters (cp);
        lastCp  = cp;
        cpValid = true;
    }
    compressor.process (mainBus);
    grMeterDb.store (compressor.getGainReductionDb());

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
