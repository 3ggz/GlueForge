#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

#include "dsp/Compressor.h"
#include "dsp/SidechainFilter.h"
#include "dsp/TempoDucker.h"
#include "dsp/Saturator.h"
#include "dsp/Multiband.h"

/**
    GlueForge — a real-time-safe stereo dynamics processor.

    Signal chain (per block): input meter → latency-compensated bypass → host
    transport → detection signal (internal / external sidechain) + SC filter →
    SC-listen audition → lookahead delay → dry copy → M/S encode → wet stage
    (tempo-duck volume-shaper | 3-band multiband | single compressor) → M/S decode
    → character saturation (oversampled when enabled) → parallel wet/dry mix +
    output gain → output meter. Reports lookahead + oversampling latency (PDC).

    All DSP lives in header-only units under Source/dsp/; APVTS holds the ~48
    parameters for automation, preset and session recall.
*/
class GlueForgeProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    GlueForgeProcessor();
    ~GlueForgeProcessor() override;

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

    // Metering snapshots of the last processed block (dB) — for the editor.
    float getCurrentGainReductionDb() const { return grMeterDb.load(); }
    float getInputLevelDb()  const { return inLevelDb.load(); }
    float getOutputLevelDb() const { return outLevelDb.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int    kDelayLineCapacity = 8192;  // samples; covers 10 ms lookahead + OS
    static constexpr int    kMaxDelaySamples   = 8000;  // lookahead clamp (< capacity)
    static constexpr double kSmoothingSeconds  = 0.02;  // 20 ms ramp for gain/mix smoothers

    // Returns the oversampler selected by the oversampling param, or nullptr (Off).
    juce::dsp::Oversampling<float>* activeOversampler();

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
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<float>* midsideParam   = nullptr;
    std::atomic<float>* mbEnableParam  = nullptr;
    std::atomic<float>* mbXLowParam    = nullptr;
    std::atomic<float>* mbXHighParam   = nullptr;
    std::atomic<float>* mbTrimParam[3]   = { nullptr, nullptr, nullptr };
    std::atomic<float>* mbBypassParam[3] = { nullptr, nullptr, nullptr };
    std::atomic<float>* mbSoloParam    = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    gf::dsp::Compressor compressor;
    gf::dsp::SidechainFilter scFilter;                  // detector-path HP/LP
    gf::dsp::TempoDucker ducker;                        // tempo-synced volume shaper
    gf::dsp::Saturator saturator;                       // character coloration
    gf::dsp::Multiband multiband;                       // 3-band LR4 multiband

    // Lookahead delay + oversampling (both contribute reported latency / PDC).
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lookaheadDelay { kDelayLineCapacity };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { kDelayLineCapacity };    // matches OS latency
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypassDelay { kDelayLineCapacity }; // latency-compensated bypass
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers; // 2x / 4x / 8x
    double currentSr = 44100.0;

    void updateLatency();
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    gf::dsp::CompressorParameters lastCp;               // last applied params (change detection)
    bool cpValid = false;
    juce::AudioBuffer<float> detectionBuffer;           // key signal fed to the detector
    juce::AudioBuffer<float> dryBuffer;                 // dry copy for parallel (wet/dry) mix
    juce::LinearSmoothedValue<float> mixSmoothed;       // wet/dry, smoothed
    juce::LinearSmoothedValue<float> gainSmoothed;      // output gain (linear), smoothed
    std::atomic<float> grMeterDb  { 0.0f };
    std::atomic<float> inLevelDb  { -100.0f };
    std::atomic<float> outLevelDb { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueForgeProcessor)
};
