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

TEST_CASE ("Processor: input/output level meters track the signal", "[processor][phase6]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::ratio, 1.0f); // transparent
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
        proc.processBlock (buffer, midi);
    }

    const float expected = juce::Decibels::gainToDecibels (0.5f, -100.0f); // ~ -6.02 dB
    REQUIRE (approxEq (proc.getInputLevelDb(),  expected, 0.3f));
    REQUIRE (approxEq (proc.getOutputLevelDb(), expected, 0.3f)); // transparent path
}

TEST_CASE ("Processor: lookahead delays audio and reports its latency", "[processor][phase7]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::ratio, 1.0f);     // transparent compressor
    setParamDb (proc, gf::params::id::lookahead, 1.0f); // 1 ms
    proc.prepareToPlay (48000.0, 512);

    const int L = (int) std::lround (1.0 * 48000.0 / 1000.0); // 48 samples
    REQUIRE (proc.getLatencySamples() == L);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    proc.processBlock (buffer, midi);

    REQUIRE (std::abs (buffer.getSample (0, 0)) < 0.01f);  // impulse delayed away from sample 0
    REQUIRE (std::abs (buffer.getSample (0, L)) > 0.9f);   // ...and lands at L
}

TEST_CASE ("Processor: oversampling adds reported latency", "[processor][phase7]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::oversampling, 2.0f); // index 2 = 4x
    proc.prepareToPlay (48000.0, 512);
    REQUIRE (proc.getLatencySamples() > 0);                // FIR oversampler latency
}

TEST_CASE ("Processor: mid/side mode changes the detection domain", "[processor][phase8]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto outL = [] (bool ms) -> float
    {
        GlueForgeProcessor proc;
        setParamDb (proc, gf::params::id::threshold, -9.0f);
        setParamDb (proc, gf::params::id::ratio,      8.0f);
        setParamDb (proc, gf::params::id::knee,       0.0f);
        setParamDb (proc, gf::params::id::link,       0.0f); // unlinked
        setParamDb (proc, gf::params::id::attack,     1.0f);
        setParamDb (proc, gf::params::id::release,    50.0f);
        setParamDb (proc, gf::params::id::midside, ms ? 1.0f : 0.0f);
        proc.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        float last = 0.0f;
        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.5f); buf.setSample (1, i, 0.0f); }
            proc.processBlock (buf, midi);
            last = std::abs (buf.getSample (0, 511));
        }
        return last;
    };

    // L=0.5 (-6 dB) > -9 dB -> compressed in L/R mode.
    REQUIRE (outL (false) < 0.45f);
    // In M/S, M=S=0.25 (-12 dB) < -9 dB -> no compression; L reconstructs to ~0.5.
    REQUIRE (outL (true) > 0.48f);
}

TEST_CASE ("Processor: parallel mix stays phase-aligned with oversampling on", "[processor][phase9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::ratio, 1.0f);        // transparent compressor
    setParamDb (proc, gf::params::id::mix, 0.5f);          // 50% parallel
    setParamDb (proc, gf::params::id::oversampling, 2.0f); // 4x (adds wet-path latency)
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const double sr = 48000.0;
    double inAcc = 0.0, outAcc = 0.0; int counted = 0;
    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (b * 512 + i) / sr);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        if (b >= 40) for (int i = 0; i < 512; ++i) inAcc += (double) buf.getSample (0, i) * buf.getSample (0, i);
        proc.processBlock (buf, midi);
        if (b >= 40) for (int i = 0; i < 512; ++i) { const float y = buf.getSample (0, i); outAcc += (double) y * y; ++counted; }
    }
    const float inRms  = (float) std::sqrt (inAcc  / juce::jmax (1, counted));
    const float outRms = (float) std::sqrt (outAcc / juce::jmax (1, counted));
    // Dry delayed to match the wet's oversampler latency -> reconstructs ~unity.
    // Without the alignment fix the 50/50 sum combs and this drops well below.
    REQUIRE (approxEq (outRms, inRms, inRms * 0.1f));
}

TEST_CASE ("Processor: bypass is latency-compensated", "[processor][phase9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::lookahead, 2.0f); // 2 ms reported latency
    proc.apvts.getParameter (gf::params::id::bypass)->setValueNotifyingHost (1.0f); // bypass on
    proc.prepareToPlay (48000.0, 512);

    const int L = (int) std::lround (2.0 * 48000.0 / 1000.0); // 96 samples
    REQUIRE (proc.getLatencySamples() == L);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    proc.processBlock (buffer, midi);

    // Even bypassed, the signal must be delayed by the reported latency.
    REQUIRE (std::abs (buffer.getSample (0, 0)) < 0.01f);
    REQUIRE (std::abs (buffer.getSample (0, L)) > 0.9f);
}

TEST_CASE ("Processor: state round-trips with many parameters set", "[state][phase9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    namespace id = gf::params::id;

    GlueForgeProcessor a;
    setParamDb (a, id::threshold, -22.0f); setParamDb (a, id::ratio, 6.0f); setParamDb (a, id::knee, 3.0f);
    setParamDb (a, id::attack, 7.0f);      setParamDb (a, id::release, 250.0f); setParamDb (a, id::hold, 30.0f);
    setParamDb (a, id::makeup, 4.0f);      setParamDb (a, id::detector, 0.7f); setParamDb (a, id::mix, 0.6f);
    setParamDb (a, id::range, 12.0f);      setParamDb (a, id::link, 0.3f);     setParamDb (a, id::gain, -3.0f);
    setParamDb (a, id::trigger, 1.0f);     setParamDb (a, id::character, 2.0f); setParamDb (a, id::drive, 0.8f);
    setParamDb (a, id::satMix, 0.5f);      setParamDb (a, id::lookahead, 3.0f); setParamDb (a, id::oversampling, 2.0f);
    setParamDb (a, id::midside, 1.0f);     setParamDb (a, id::mbEnable, 1.0f);  setParamDb (a, id::mbXLow, 150.0f);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    GlueForgeProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    for (const char* pid : { id::threshold, id::ratio, id::knee, id::attack, id::release, id::hold,
                             id::makeup, id::detector, id::mix, id::range, id::link, id::gain, id::drive,
                             id::satMix, id::lookahead, id::mbXLow, id::trigger, id::character,
                             id::oversampling, id::midside, id::mbEnable })
        REQUIRE (approxEq (a.apvts.getRawParameterValue (pid)->load(),
                           b.apvts.getRawParameterValue (pid)->load(), 0.01f));
}

TEST_CASE ("Processor: pump shape survives state save/restore", "[state][shape]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor a;
    a.setDuckShapeString ("0.0:0.2 0.25:0.9 0.6:0.3 1.0:1.0"); // non-default shape (state-tree property)
    const auto saved = a.getDuckShapeString();
    REQUIRE (saved.isNotEmpty());

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    GlueForgeProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    REQUIRE (b.getDuckShapeString() == saved); // the shape is not a parameter, so this is the only guard
}

TEST_CASE ("Processor: SC-listen outputs the detection signal, not the compressed output", "[processor][phase9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::threshold, -60.0f); // would crush hard...
    setParamDb (proc, gf::params::id::ratio, 20.0f);
    setParamDb (proc, gf::params::id::scListen, 1.0f);    // ...but SC-listen bypasses compression
    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float last = 0.0f;
    for (int b = 0; b < 8; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
        proc.processBlock (buffer, midi);
        last = std::abs (buffer.getSample (0, 256));
    }
    // Detection (= main, default SC filter bypassed) passes at unity, uncompressed.
    REQUIRE (approxEq (last, 0.5f, 0.05f));
}

TEST_CASE ("Processor: M/S round-trips asymmetric stereo when transparent", "[processor][phase9]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    GlueForgeProcessor proc;
    setParamDb (proc, gf::params::id::ratio, 1.0f);    // transparent compressor
    setParamDb (proc, gf::params::id::midside, 1.0f);  // M/S on
    proc.prepareToPlay (48000.0, 64);

    juce::AudioBuffer<float> buf (2, 64);
    for (int i = 0; i < 64; ++i) { buf.setSample (0, i, 0.4f * std::sin (i * 0.3f)); buf.setSample (1, i, -0.25f * std::cos (i * 0.2f)); }
    juce::AudioBuffer<float> in; in.makeCopyOf (buf);

    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);

    // encode -> (transparent) -> decode must reconstruct the asymmetric L/R exactly.
    for (int i = 0; i < 64; ++i)
    {
        REQUIRE (approxEq (buf.getSample (0, i), in.getSample (0, i), 1.0e-4f));
        REQUIRE (approxEq (buf.getSample (1, i), in.getSample (1, i), 1.0e-4f));
    }
}
