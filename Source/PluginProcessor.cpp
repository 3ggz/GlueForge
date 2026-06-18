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
    gainParam   = apvts.getRawParameterValue (gf::params::id::gain);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (gf::params::id::bypass));
    jassert (gainParam != nullptr && bypassParam != nullptr);
}

void GlueForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    gainSmoothed.reset (sampleRate, 0.02); // 20 ms ramp — click-free
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));

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
        // Bypassed: audio passes through untouched. Keep the smoother in sync so
        // re-engaging does not jump.
        gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
        return;
    }

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
