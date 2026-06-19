#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

#include "dsp/Compressor.h"
#include "dsp/SidechainFilter.h"
#include "dsp/TempoDucker.h"
#include "dsp/Saturator.h"

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

    // Peak gain reduction (dB, <= 0) of the last processed block — for the GR meter.
    float getCurrentGainReductionDb() const { return grMeterDb.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    // Cached raw parameter pointers (read on the audio thread).
    std::atomic<float>* gainParam      = nullptr; // output gain, dB
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* kneeParam       = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* holdParam      = nullptr;
    std::atomic<float>* makeupParam    = nullptr;
    std::atomic<float>* detectorParam  = nullptr;
    std::atomic<float>* mixParam       = nullptr;
    std::atomic<float>* rangeParam     = nullptr;
    std::atomic<float>* linkParam      = nullptr;
    std::atomic<float>* autoMakeupParam = nullptr;
    std::atomic<float>* triggerParam   = nullptr; // 0 Internal, 1 External SC, 2 Tempo Duck
    std::atomic<float>* scHpfParam     = nullptr;
    std::atomic<float>* scLpfParam     = nullptr;
    std::atomic<float>* scListenParam  = nullptr;
    std::atomic<float>* duckRateParam  = nullptr;
    std::atomic<float>* duckDepthParam = nullptr;
    std::atomic<float>* duckCurveParam = nullptr;
    std::atomic<float>* syncReleaseParam = nullptr;
    std::atomic<float>* releaseDivParam  = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* driveParam     = nullptr;
    std::atomic<float>* satMixParam    = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    gf::dsp::Compressor compressor;
    gf::dsp::SidechainFilter scFilter;                  // detector-path HP/LP
    gf::dsp::TempoDucker ducker;                        // tempo-synced volume shaper
    gf::dsp::Saturator saturator;                       // character coloration
    gf::dsp::CompressorParameters lastCp;               // last applied params (change detection)
    bool cpValid = false;
    juce::AudioBuffer<float> detectionBuffer;           // key signal fed to the detector
    juce::AudioBuffer<float> dryBuffer;                 // dry copy for parallel (wet/dry) mix
    juce::LinearSmoothedValue<float> mixSmoothed;       // wet/dry, smoothed
    juce::LinearSmoothedValue<float> gainSmoothed;      // output gain (linear), smoothed
    std::atomic<float> grMeterDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueForgeProcessor)
};
