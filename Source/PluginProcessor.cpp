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
    makeupParam    = apvts.getRawParameterValue (gf::params::id::makeup);
    detectorParam  = apvts.getRawParameterValue (gf::params::id::detector);
    bypassParam    = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gf::params::id::bypass));
    jassert (gainParam != nullptr && bypassParam != nullptr && thresholdParam != nullptr);
}

void GlueForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    gainSmoothed.reset (sampleRate, 0.02); // 20 ms ramp — click-free
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));

    compressor.prepare (sampleRate, juce::jmax (1, getMainBusNumOutputChannels()));

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
        grMeterDb.store (0.0f);
        return;
    }

    // 1) Compression (detector -> static curve -> ballistics -> gain + makeup).
    gf::dsp::CompressorParameters cp;
    cp.thresholdDb   = thresholdParam->load();
    cp.ratio         = ratioParam->load();
    cp.kneeDb        = kneeParam->load();
    cp.attackMs      = attackParam->load();
    cp.releaseMs     = releaseParam->load();
    cp.holdMs        = holdParam->load();
    cp.makeupDb      = makeupParam->load();
    cp.detectorBlend = detectorParam->load();
    compressor.setParameters (cp);
    compressor.process (mainBus);
    grMeterDb.store (compressor.getGainReductionDb());

    // 2) Output gain (post-compression trim), smoothed to stay click-free.
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = gainSmoothed.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            mainBus.getWritePointer (ch)[n] *= g;
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
