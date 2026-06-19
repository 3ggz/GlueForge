#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "ParamIDs.h"

#include <cmath>

namespace
{
    bool approxEq (float a, float b, float tol = 1.0e-4f)
    {
        return std::abs (a - b) <= tol;
    }

    void setParamDb (GlueForgeProcessor& p, const char* id, float db)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (db));
    }
}

TEST_CASE ("Gain parameter scales the signal by the expected linear factor", "[dsp][phase1]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;

    constexpr double sr        = 48000.0;
    constexpr int    blockSize = 512;
    constexpr float  inputVal  = 0.25f;
    constexpr float  gainDb    = 6.0f;

    setParamDb (proc, gf::params::id::ratio, 1.0f); // 1:1 => no compression, isolate output gain
    setParamDb (proc, gf::params::id::gain, gainDb);
    proc.prepareToPlay (sr, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Process several blocks so the gain smoother reaches steady state.
    float lastSample = 0.0f;
    for (int block = 0; block < 16; ++block)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), inputVal, blockSize);

        proc.processBlock (buffer, midi);
        lastSample = buffer.getSample (0, blockSize - 1);
    }

    const float expected = inputVal * juce::Decibels::decibelsToGain (gainDb);
    REQUIRE (approxEq (lastSample, expected, 1.0e-3f));
}

TEST_CASE ("Bypass leaves the signal untouched", "[dsp][phase1]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;

    setParamDb (proc, gf::params::id::gain, 12.0f); // would be obvious if applied
    auto* bypass = proc.apvts.getParameter (gf::params::id::bypass);
    bypass->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.3f, 256);

    proc.processBlock (buffer, midi);

    REQUIRE (approxEq (buffer.getSample (0, 128), 0.3f));
}

TEST_CASE ("Plugin state round-trips through save/restore", "[state][phase1]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor a;
    setParamDb (a, gf::params::id::gain, -7.5f);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    GlueForgeProcessor b;
    b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    const float va = a.apvts.getRawParameterValue (gf::params::id::gain)->load();
    const float vb = b.apvts.getRawParameterValue (gf::params::id::gain)->load();

    REQUIRE (approxEq (va, -7.5f, 0.01f));
    REQUIRE (approxEq (vb, va));
}

TEST_CASE ("Reported latency is zero in Phase 1", "[latency][phase1]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    proc.prepareToPlay (44100.0, 128);
    REQUIRE (proc.getLatencySamples() == 0);
}

TEST_CASE ("Processor applies gain reduction when driven above threshold", "[processor][phase2]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::threshold, -40.0f);
    setParamDb (proc, gf::params::id::ratio,     10.0f);
    setParamDb (proc, gf::params::id::knee,      0.0f);
    setParamDb (proc, gf::params::id::attack,    1.0f);
    setParamDb (proc, gf::params::id::release,   50.0f);
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
        proc.processBlock (buffer, midi);
    }

    // 0.5 (-6 dBFS) vs threshold -40, ratio 10 -> deep, sustained reduction.
    REQUIRE (proc.getCurrentGainReductionDb() < -10.0f);
}

TEST_CASE ("Processor: parallel mix at 0% passes the dry signal", "[processor][phase3]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::threshold, -40.0f); // would compress hard...
    setParamDb (proc, gf::params::id::ratio,     10.0f);
    setParamDb (proc, gf::params::id::mix,        0.0f);  // ...but mix is fully dry
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 10; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
        proc.processBlock (buffer, midi);
    }

    REQUIRE (approxEq (buffer.getSample (0, 256), 0.5f, 1.0e-3f));
}

TEST_CASE ("Processor: tempo-duck mode modulates the level", "[processor][phase4]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::trigger,   2.0f); // Tempo Duck
    setParamDb (proc, gf::params::id::duckDepth, 24.0f);
    setParamDb (proc, gf::params::id::duckRate,  2.0f); // 1/4
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float mn = 1.0e9f, mx = -1.0e9f;
    for (int b = 0; b < 200; ++b) // ~2.1 s -> several 1/4-note cycles at the 120 BPM fallback
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, 512);
        proc.processBlock (buffer, midi);
        for (int i = 0; i < 512; ++i)
        {
            const float v = std::abs (buffer.getSample (0, i));
            mn = juce::jmin (mn, v);
            mx = juce::jmax (mx, v);
        }
    }
    // Envelope sweeps from minGain (~ -24 dB) up to ~unity each cycle.
    REQUIRE (mx > 0.9f);
    REQUIRE (mn < 0.2f);
}
