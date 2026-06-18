#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

/**
    GlueForge — Phase 1 skeleton.

    A real-time-safe stereo plugin with a single smoothed Gain parameter, a soft
    Bypass, APVTS-backed state, a declared (disabled-by-default) sidechain input
    bus, and a wired-up latency-reporting hook. No compressor DSP yet — Phase 1's
    only job is proving host + toolchain integration.
*/
class GlueForgeProcessor : public juce::AudioProcessor
{
public:
    GlueForgeProcessor();
    ~GlueForgeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GlueForge"; }
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

    // Lets the host provide click-free, latency-compensated bypass.
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

private:
    std::atomic<float>*        gainParam   = nullptr;   // dB
    juce::AudioParameterBool*  bypassParam = nullptr;
    juce::LinearSmoothedValue<float> gainSmoothed;      // linear gain, smoothed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueForgeProcessor)
};
