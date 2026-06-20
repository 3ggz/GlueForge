#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/GainComputer.h"
#include "dsp/BallisticsSmoother.h"
#include "dsp/LevelDetector.h"
#include "dsp/Compressor.h"
#include "dsp/SidechainFilter.h"
#include "dsp/TempoDucker.h"
#include "dsp/TempoSync.h"
#include "dsp/Saturator.h"
#include "dsp/Multiband.h"
#include "dsp/DuckShape.h"

#include <cmath>
#include <vector>

using namespace gf::dsp;

namespace
{
    bool approx (float a, float b, float tol) { return std::abs (a - b) <= tol; }

    float sineSample (float amp, float freq, double sr, int i)
    {
        return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
    }
}

// ───────────────────────── GainComputer (exact static curve) ─────────────────────────

TEST_CASE ("GainComputer: no reduction below threshold (hard knee)", "[dsp][gaincomputer]")
{
    GainComputer gc; gc.setParameters (-10.0f, 4.0f, 0.0f);
    REQUIRE (approx (gc.computeGainReductionDb (-20.0f), 0.0f, 1.0e-4f));
}

TEST_CASE ("GainComputer: hard-knee reduction above threshold", "[dsp][gaincomputer]")
{
    GainComputer gc; gc.setParameters (-10.0f, 4.0f, 0.0f);
    // input 0 dB, T=-10, R=4 -> ydB = -10 + 10/4 = -7.5 -> GR = -7.5
    REQUIRE (approx (gc.computeGainReductionDb (0.0f), -7.5f, 1.0e-3f));
}

TEST_CASE ("GainComputer: high ratio approaches limiting", "[dsp][gaincomputer]")
{
    GainComputer gc; gc.setParameters (-10.0f, 1000.0f, 0.0f);
    REQUIRE (approx (gc.computeGainReductionDb (0.0f), -10.0f, 0.05f));
}

TEST_CASE ("GainComputer: soft knee at threshold centre", "[dsp][gaincomputer]")
{
    GainComputer gc; gc.setParameters (-10.0f, 4.0f, 6.0f);
    // overshoot 0 -> GR = (1/4-1)*(0+3)^2/(2*6) = -0.75 * 9/12 = -0.5625
    REQUIRE (approx (gc.computeGainReductionDb (-10.0f), -0.5625f, 1.0e-3f));
}

TEST_CASE ("GainComputer: soft knee is continuous at the lower edge", "[dsp][gaincomputer]")
{
    GainComputer gc; gc.setParameters (-10.0f, 4.0f, 6.0f);
    // lower edge: overshoot = -3 (input -13) -> GR = 0
    REQUIRE (approx (gc.computeGainReductionDb (-13.0f), 0.0f, 1.0e-3f));
}

TEST_CASE ("GainComputer: soft knee meets hard knee at the upper edge", "[dsp][gaincomputer]")
{
    GainComputer soft; soft.setParameters (-10.0f, 4.0f, 6.0f);
    GainComputer hard; hard.setParameters (-10.0f, 4.0f, 0.0f);
    // upper edge: overshoot = +3 (input -7) -> soft == hard == -2.25
    REQUIRE (approx (soft.computeGainReductionDb (-7.0f), hard.computeGainReductionDb (-7.0f), 1.0e-3f));
    REQUIRE (approx (soft.computeGainReductionDb (-7.0f), -2.25f, 1.0e-3f));
}

// ───────────────────────── BallisticsSmoother (attack/release/hold) ─────────────────────────

TEST_CASE ("BallisticsSmoother: reaches ~63% of a step after one attack time-constant", "[dsp][ballistics]")
{
    BallisticsSmoother b; b.prepare (48000.0); b.setTimes (10.0f, 100.0f, 0.0f); b.reset (0.0f);
    const int n = (int) std::round (0.010 * 48000.0);
    float v = 0.0f;
    for (int i = 0; i < n; ++i) v = b.processSample (-10.0f);
    REQUIRE (approx (v, -10.0f * (1.0f - std::exp (-1.0f)), 0.2f)); // ~ -6.32
}

TEST_CASE ("BallisticsSmoother: attack time-constant is sample-rate independent", "[dsp][ballistics]")
{
    BallisticsSmoother b1; b1.prepare (44100.0); b1.setTimes (20.0f, 200.0f, 0.0f); b1.reset (0.0f);
    BallisticsSmoother b2; b2.prepare (96000.0); b2.setTimes (20.0f, 200.0f, 0.0f); b2.reset (0.0f);
    float v1 = 0.0f, v2 = 0.0f;
    for (int i = 0; i < (int) std::round (0.020 * 44100.0); ++i) v1 = b1.processSample (-12.0f);
    for (int i = 0; i < (int) std::round (0.020 * 96000.0); ++i) v2 = b2.processSample (-12.0f);
    REQUIRE (approx (v1, v2, 0.05f));
}

TEST_CASE ("BallisticsSmoother: hold delays the release", "[dsp][ballistics]")
{
    BallisticsSmoother b; b.prepare (48000.0); b.setTimes (0.0f, 100.0f, 20.0f); b.reset (0.0f);
    const float settled = b.processSample (-10.0f);  // attack 0 -> instant
    REQUIRE (approx (settled, -10.0f, 1.0e-3f));

    const int holdN = (int) std::round (0.020 * 48000.0);
    float during = -10.0f;
    for (int i = 0; i < holdN - 2; ++i) during = b.processSample (0.0f);
    REQUIRE (approx (during, -10.0f, 1.0e-2f));        // still held

    for (int i = 0; i < 20000; ++i) b.processSample (0.0f);
    REQUIRE (b.getCurrent() > -1.0f);                  // released essentially fully
}

// ───────────────────────── LevelDetector (peak / RMS / blend) ─────────────────────────

TEST_CASE ("LevelDetector: peak mode tracks sine amplitude (~0 dB rel A)", "[dsp][detector]")
{
    LevelDetector d; d.prepare (48000.0); d.setBlend (0.0f); d.reset();
    const float A = 1.0f; const double sr = 48000.0;
    float lvl = 0.0f;
    for (int i = 0; i < (int) (0.2 * sr); ++i) lvl = d.processSample (sineSample (A, 1000.0f, sr, i));
    REQUIRE (approx (juce::Decibels::gainToDecibels (lvl, -100.0f), 0.0f, 0.5f));
}

TEST_CASE ("LevelDetector: RMS mode of a sine is -3.01 dB", "[dsp][detector]")
{
    LevelDetector d; d.prepare (48000.0); d.setBlend (1.0f); d.reset();
    const float A = 1.0f; const double sr = 48000.0;
    float lvl = 0.0f;
    for (int i = 0; i < (int) (0.3 * sr); ++i) lvl = d.processSample (sineSample (A, 1000.0f, sr, i));
    REQUIRE (approx (juce::Decibels::gainToDecibels (lvl, -100.0f), -3.01f, 0.3f));
}

TEST_CASE ("LevelDetector: constant input reports its amplitude in both modes", "[dsp][detector]")
{
    for (float blend : { 0.0f, 1.0f })
    {
        LevelDetector d; d.prepare (48000.0); d.setBlend (blend); d.reset();
        float lvl = 0.0f;
        for (int i = 0; i < 5000; ++i) lvl = d.processSample (0.5f);
        REQUIRE (approx (lvl, 0.5f, 1.0e-3f));
    }
}

// ───────────────────────── Compressor integration (the GR-matches-curve gate) ─────────────────────────

TEST_CASE ("Compressor: steady-state GR matches the static curve for a constant-level signal", "[dsp][compressor][gate]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -12.0f; p.ratio = 4.0f; p.kneeDb = 0.0f;
    p.attackMs = 5.0f; p.releaseMs = 50.0f; p.holdMs = 0.0f;
    p.makeupDb = 0.0f; p.detectorBlend = 0.0f;
    comp.setParameters (p);

    const float A = 0.5f; // -6.0206 dBFS; peak == rms == A for a constant-magnitude signal
    GainComputer ref; ref.setParameters (-12.0f, 4.0f, 0.0f);
    const float expectedGr = ref.computeGainReductionDb (juce::Decibels::gainToDecibels (A, -100.0f));

    juce::AudioBuffer<float> buf (2, 512);
    float lastGr = 0.0f;
    const int blocks = (int) std::ceil (0.3 * 48000.0 / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, A); buf.setSample (1, i, A); }
        comp.process (buf);
        lastGr = comp.getGainReductionDb();
    }

    REQUIRE (approx (lastGr, expectedGr, 0.1f));
    const float expectedGain = juce::Decibels::decibelsToGain (expectedGr);
    REQUIRE (approx (std::abs (buf.getSample (0, 511)), A * expectedGain, 0.01f));
}

TEST_CASE ("Compressor: gain reduction moves continuously under a sudden level jump (no zipper)", "[dsp][compressor]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -30.0f; p.ratio = 8.0f; p.kneeDb = 0.0f;
    p.attackMs = 10.0f; p.releaseMs = 150.0f; p.holdMs = 0.0f;
    p.makeupDb = 0.0f; p.detectorBlend = 0.0f;
    comp.setParameters (p);

    juce::AudioBuffer<float> one (2, 1);
    auto pushSample = [&] (float s) { one.setSample (0, 0, s); one.setSample (1, 0, s); comp.process (one); };

    for (int i = 0; i < 2000; ++i) pushSample (0.0f);   // silence

    std::vector<float> gr; gr.reserve (3000);
    for (int i = 0; i < 3000; ++i) { pushSample (0.8f); gr.push_back (comp.getGainReductionDb()); }

    float maxStep = 0.0f;
    for (size_t i = 1; i < gr.size(); ++i) maxStep = std::max (maxStep, std::abs (gr[i] - gr[i - 1]));

    REQUIRE (maxStep < 1.0f);     // one-pole ballistics -> tiny per-sample change
    REQUIRE (gr.back() < -10.0f); // and it did clamp down hard
}

// ───────────────────────── Phase 3: range / auto-makeup / stereo link ─────────────────────────

namespace
{
    void runConstant (Compressor& comp, float L, float R, int blocks = 40, int block = 512)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, L); buf.setSample (1, i, R); }
            comp.process (buf);
        }
    }
}

TEST_CASE ("Compressor: range caps the maximum gain reduction", "[dsp][compressor][phase3]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -30.0f; p.ratio = 8.0f; p.kneeDb = 0.0f;
    p.attackMs = 5.0f; p.releaseMs = 50.0f; p.rangeDb = 6.0f; // would be ~ -21 dB uncapped
    comp.setParameters (p);

    runConstant (comp, 0.5f, 0.5f, 30);
    REQUIRE (approx (comp.getGainReductionDb(), -6.0f, 0.05f));
}

TEST_CASE ("Compressor: auto-makeup yields ~unity gain at 0 dBFS", "[dsp][compressor][phase3]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -18.0f; p.ratio = 4.0f; p.kneeDb = 0.0f;
    p.attackMs = 5.0f; p.releaseMs = 50.0f; p.autoMakeup = true;
    comp.setParameters (p);

    juce::AudioBuffer<float> buf (2, 256);
    for (int b = 0; b < 80; ++b)
    {
        for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 1.0f); buf.setSample (1, i, 1.0f); }
        comp.process (buf);
    }
    // 0 dBFS in; auto-makeup compensates the curve's reduction at 0 dBFS -> ~unity out
    REQUIRE (approx (std::abs (buf.getSample (0, 255)), 1.0f, 0.02f));
}

TEST_CASE ("Compressor: unlinked detection compresses channels independently", "[dsp][compressor][phase3]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -20.0f; p.ratio = 8.0f; p.kneeDb = 0.0f;
    p.attackMs = 2.0f; p.releaseMs = 50.0f; p.stereoLink = 0.0f;
    comp.setParameters (p);

    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.5f); buf.setSample (1, i, 0.001f); }
        comp.process (buf);
    }
    // L (loud) reduced; R (well below threshold) essentially untouched
    REQUIRE (std::abs (buf.getSample (0, 511)) < 0.5f);
    REQUIRE (std::abs (buf.getSample (1, 511)) > 0.0009f);
}

TEST_CASE ("Compressor: full link applies the louder channel's reduction to both", "[dsp][compressor][phase3]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -20.0f; p.ratio = 8.0f; p.kneeDb = 0.0f;
    p.attackMs = 2.0f; p.releaseMs = 50.0f; p.stereoLink = 1.0f;
    comp.setParameters (p);

    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.5f); buf.setSample (1, i, 0.001f); }
        comp.process (buf);
    }
    // linked: R is pulled down by L's reduction, well below its 0.001 input
    REQUIRE (std::abs (buf.getSample (1, 511)) < 0.0005f);
}

// ───────────────────────── Phase 4a: external sidechain + SC filter ─────────────────────────

namespace
{
    float steadyRms (SidechainFilter& f, float freq, double sr = 48000.0)
    {
        f.reset();
        const int N = (int) (0.2 * sr);
        double acc = 0.0; int counted = 0;
        for (int i = 0; i < N; ++i)
        {
            const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
            const float y = f.processSample (0, x);
            if (i > N / 2) { acc += (double) y * y; ++counted; }
        }
        return (float) std::sqrt (acc / juce::jmax (1, counted));
    }
}

TEST_CASE ("SidechainFilter: high-pass attenuates lows and passes highs", "[dsp][scfilter][phase4]")
{
    SidechainFilter f; f.prepare (48000.0, 1);
    f.setCutoffs (500.0f, 20000.0f);              // HPF 500 Hz, LPF effectively off
    REQUIRE (steadyRms (f, 50.0f)   < 0.2f);      // a decade below cutoff -> strongly attenuated
    REQUIRE (steadyRms (f, 5000.0f) > 0.6f);      // a decade above -> passes (sine RMS ~0.707)
}

TEST_CASE ("SidechainFilter: low-pass attenuates highs and passes lows", "[dsp][scfilter][phase4]")
{
    SidechainFilter f; f.prepare (48000.0, 1);
    f.setCutoffs (20.0f, 500.0f);                 // HPF off, LPF 500 Hz
    REQUIRE (steadyRms (f, 50.0f)   > 0.6f);
    REQUIRE (steadyRms (f, 5000.0f) < 0.2f);
}

TEST_CASE ("SidechainFilter: bypasses at extreme settings (constant signal preserved)", "[dsp][scfilter][phase4]")
{
    SidechainFilter f; f.prepare (48000.0, 1);
    f.setCutoffs (20.0f, 20000.0f);               // both at extremes -> pass-through
    float y = 0.0f;
    for (int i = 0; i < 1000; ++i) y = f.processSample (0, 0.5f); // DC must survive
    REQUIRE (approx (y, 0.5f, 1.0e-4f));
}

TEST_CASE ("Compressor: external detection ducks a quiet main when the key is loud", "[dsp][compressor][phase4]")
{
    Compressor comp; comp.prepare (48000.0, 2);
    CompressorParameters p;
    p.thresholdDb = -30.0f; p.ratio = 8.0f; p.kneeDb = 0.0f;
    p.attackMs = 2.0f; p.releaseMs = 50.0f; p.stereoLink = 1.0f;
    comp.setParameters (p);

    juce::AudioBuffer<float> main (2, 512), key (2, 512);
    float lastMain = 0.0f;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            main.setSample (0, i, 0.05f); main.setSample (1, i, 0.05f); // quiet
            key.setSample  (0, i, 0.5f);  key.setSample  (1, i, 0.5f);  // loud key
        }
        comp.process (main, &key);
        lastMain = main.getSample (0, 511);
    }

    GainComputer ref; ref.setParameters (-30.0f, 8.0f, 0.0f);
    const float expGain = juce::Decibels::decibelsToGain (
        ref.computeGainReductionDb (juce::Decibels::gainToDecibels (0.5f, -100.0f)));
    // The quiet main is reduced by the KEY-driven gain reduction (detection decoupled).
    REQUIRE (approx (std::abs (lastMain), 0.05f * expGain, 0.001f));
}

// ───────────────────────── Phase 4b: tempo-synced ducking + sync helpers ─────────────────────────

TEST_CASE ("TempoSync: note division converts to milliseconds", "[dsp][temposync][phase4]")
{
    REQUIRE (approx ((float) beatsToMs (divisionBeats (3), 120.0), 250.0f, 0.01f)); // 1/8 @120 = 250 ms
    REQUIRE (approx ((float) beatsToMs (divisionBeats (2), 120.0), 500.0f, 0.01f)); // 1/4 @120 = 500 ms
}

TEST_CASE ("TempoDucker: advance wraps the phase within [0,1)", "[dsp][ducker][phase4]")
{
    TempoDucker d; d.prepare (48000.0); d.setRate (120.0, 1.0); d.reset();
    float prev = 0.0f; bool wrapped = false;
    for (int i = 0; i < 48000; ++i) // 1 s at 120 BPM = two 1-beat cycles
    {
        const float p = d.advance();
        REQUIRE (p >= 0.0f);
        REQUIRE (p <  1.0f);
        if (p < prev) wrapped = true;
        prev = p;
    }
    REQUIRE (wrapped);
}

TEST_CASE ("TempoDucker: syncToPpq sets the phase", "[dsp][ducker][phase4]")
{
    TempoDucker d; d.prepare (48000.0); d.setRate (120.0, 1.0);
    d.syncToPpq (2.5, 1.0); // half-way through a 1-beat cycle
    REQUIRE (approx (d.currentPhase(), 0.5f, 1.0e-3f));
}

// ───────────────────────── Phase 5: character models + saturation ─────────────────────────

TEST_CASE ("Saturator: clean pass-through at mix 0", "[dsp][sat][phase5]")
{
    juce::AudioBuffer<float> b (1, 8);
    for (int i = 0; i < 8; ++i) b.setSample (0, i, 0.8f);
    Saturator s; s.prepare (48000.0, 1);
    s.setModel (CharacterModel::FET); s.setDrive (1.0f); s.setMix (0.0f);
    s.process (b);
    for (int i = 0; i < 8; ++i) REQUIRE (approx (b.getSample (0, i), 0.8f, 1.0e-6f));
}

TEST_CASE ("Saturator: small signal is ~unity", "[dsp][sat][phase5]")
{
    REQUIRE (approx (Saturator::saturateSample (CharacterModel::VCA, 0.001f, 0.5f), 0.001f, 1.0e-4f));
}

TEST_CASE ("Saturator: high drive reduces peak; FET hardest, Opto gentlest", "[dsp][sat][phase5]")
{
    const float vca  = std::abs (Saturator::saturateSample (CharacterModel::VCA,  1.0f, 1.0f));
    const float fet  = std::abs (Saturator::saturateSample (CharacterModel::FET,  1.0f, 1.0f));
    const float opto = std::abs (Saturator::saturateSample (CharacterModel::Opto, 1.0f, 1.0f));
    REQUIRE (vca  < 1.0f);
    REQUIRE (fet  < 1.0f);
    REQUIRE (opto < 1.0f);
    REQUIRE (fet < vca);   // FET clips hardest
    REQUIRE (vca < opto);  // Opto gentlest
}

TEST_CASE ("Character: FET is snappier and Opto smoother than VCA", "[dsp][character][phase5]")
{
    REQUIRE (characterAttackScale (CharacterModel::FET) < characterAttackScale (CharacterModel::VCA));
    REQUIRE (characterAttackScale (CharacterModel::VCA) < characterAttackScale (CharacterModel::Opto));
    REQUIRE (characterReleaseScale (CharacterModel::FET) < characterReleaseScale (CharacterModel::Opto));
}

// ───────────────────────── Phase 8: multiband ─────────────────────────

namespace
{
    // Runs `mb` over a continuous sine and returns steady-state output RMS (mono).
    float multibandOutRms (Multiband& mb, float freq, double sr = 48000.0, int blocks = 60)
    {
        juce::AudioBuffer<float> main (1, 512), det (1, 512);
        double acc = 0.0; int counted = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (b * 512 + i) / sr);
                main.setSample (0, i, x); det.setSample (0, i, x);
            }
            mb.process (main, det);
            if (b >= blocks / 2)
                for (int i = 0; i < 512; ++i) { const float y = main.getSample (0, i); acc += (double) y * y; ++counted; }
        }
        return (float) std::sqrt (acc / juce::jmax (1, counted));
    }
}

TEST_CASE ("Multiband: reconstructs flat with no compression", "[dsp][multiband][phase8]")
{
    Multiband mb; mb.prepare (48000.0, 1, 512); mb.setCrossovers (200.0f, 2000.0f);
    CompressorParameters p; p.ratio = 1.0f; // unity -> no gain change
    mb.setCompressorParams (p);
    for (int i = 0; i < 3; ++i) mb.setBand (i, 0.0f, false);
    mb.setSolo (-1);

    // A 1 kHz sine (RMS ~0.707) should survive the LR allpass sum nearly intact.
    REQUIRE (approx (multibandOutRms (mb, 1000.0f), 0.707f, 0.07f));
}

TEST_CASE ("Multiband: solo isolates a band's frequency range", "[dsp][multiband][phase8]")
{
    CompressorParameters p; p.ratio = 1.0f;

    Multiband hi; hi.prepare (48000.0, 1, 512); hi.setCrossovers (200.0f, 2000.0f);
    hi.setCompressorParams (p); for (int i = 0; i < 3; ++i) hi.setBand (i, 0.0f, false);
    hi.setSolo (2);                                  // solo HIGH band
    REQUIRE (multibandOutRms (hi, 100.0f) < 0.1f);   // 100 Hz (low band) removed

    Multiband lo; lo.prepare (48000.0, 1, 512); lo.setCrossovers (200.0f, 2000.0f);
    lo.setCompressorParams (p); for (int i = 0; i < 3; ++i) lo.setBand (i, 0.0f, false);
    lo.setSolo (0);                                  // solo LOW band
    REQUIRE (multibandOutRms (lo, 100.0f) > 0.5f);   // 100 Hz passes
}

TEST_CASE ("Multiband: per-band bypass passes the band uncompressed", "[dsp][multiband][phase9]")
{
    CompressorParameters p;
    p.thresholdDb = -40.0f; p.ratio = 10.0f; p.kneeDb = 0.0f; p.attackMs = 2.0f; p.releaseMs = 50.0f;

    auto run = [&] (bool lowBypassed)
    {
        Multiband mb; mb.prepare (48000.0, 1, 512); mb.setCrossovers (200.0f, 2000.0f);
        mb.setCompressorParams (p);
        mb.setBand (0, 0.0f, lowBypassed); mb.setBand (1, 0.0f, false); mb.setBand (2, 0.0f, false);
        mb.setSolo (-1);
        return multibandOutRms (mb, 100.0f); // 100 Hz lives in the low band
    };

    REQUIRE (run (true) > run (false)); // bypassed low band is louder than the compressed one
}

TEST_CASE ("Multiband: per-band trim scales the band level", "[dsp][multiband][phase9]")
{
    CompressorParameters p; p.ratio = 1.0f; // transparent

    auto run = [&] (float lowTrimDb)
    {
        Multiband mb; mb.prepare (48000.0, 1, 512); mb.setCrossovers (200.0f, 2000.0f);
        mb.setCompressorParams (p);
        mb.setBand (0, lowTrimDb, false); mb.setBand (1, 0.0f, false); mb.setBand (2, 0.0f, false);
        mb.setSolo (0); // solo low so only the trimmed band contributes
        return multibandOutRms (mb, 100.0f);
    };

    const float unity = run (0.0f), minus6 = run (-6.0f);
    REQUIRE (approx (minus6 / unity, juce::Decibels::decibelsToGain (-6.0f), 0.05f)); // ~0.5
}

TEST_CASE ("Saturator: process() blends wet and dry at mix 0.5", "[dsp][sat][phase9]")
{
    juce::AudioBuffer<float> b (1, 8);
    for (int i = 0; i < 8; ++i) b.setSample (0, i, 0.8f);

    Saturator s; s.prepare (48000.0, 1);
    s.setModel (CharacterModel::VCA); s.setDrive (1.0f); s.setMix (0.5f);
    s.process (b);

    const float expected = 0.5f * Saturator::saturateSample (CharacterModel::VCA, 0.8f, 1.0f) + 0.5f * 0.8f;
    REQUIRE (approx (b.getSample (0, 4), expected, 1.0e-5f));
}

// ───────────────────────── Editable pump shape ─────────────────────────

TEST_CASE ("DuckShape: default dips at the downbeat and recovers", "[dsp][shape]")
{
    DuckShape s;
    REQUIRE (approx (s.valueAt (0.0f),   0.0f, 0.02f)); // fully ducked
    REQUIRE (approx (s.valueAt (0.999f), 1.0f, 0.05f)); // recovered
}

TEST_CASE ("DuckShape: linear interpolation between nodes", "[dsp][shape]")
{
    DuckShape s;
    ShapeNode n[] = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    s.setNodes (n, 2);
    REQUIRE (approx (s.valueAt (0.5f), 0.5f, 0.01f));
}

TEST_CASE ("DuckShape: toString/fromString round-trips", "[dsp][shape]")
{
    DuckShape a;
    ShapeNode n[] = { { 0.0f, 0.1f }, { 0.3f, 0.8f }, { 1.0f, 0.9f } };
    a.setNodes (n, 3);
    DuckShape b;
    b.fromString (a.toString());
    REQUIRE (b.getNumNodes() == 3);
    REQUIRE (approx (a.valueAt (0.3f), b.valueAt (0.3f), 1.0e-3f));
    REQUIRE (approx (a.valueAt (0.7f), b.valueAt (0.7f), 1.0e-3f));
}

TEST_CASE ("DuckShape: fromString sanitizes (clamp, sort, endpoints)", "[dsp][shape]")
{
    DuckShape s;
    s.fromString ("0.7:2.0 0.1:-0.5 0.4:0.5"); // unsorted, out-of-range values
    REQUIRE (s.getNode (0).phase == 0.0f);                       // first endpoint forced to 0
    REQUIRE (s.getNode (s.getNumNodes() - 1).phase == 1.0f);     // last forced to 1
    for (int i = 0; i < s.getNumNodes(); ++i)
    {
        REQUIRE (s.getNode (i).value >= 0.0f);
        REQUIRE (s.getNode (i).value <= 1.0f);
        if (i > 0) REQUIRE (s.getNode (i).phase >= s.getNode (i - 1).phase); // sorted
    }
}

TEST_CASE ("DuckShape: segment curve bends the interpolation", "[dsp][shape]")
{
    DuckShape lin;  ShapeNode nl[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }; lin.setNodes (nl, 2);
    DuckShape bent; ShapeNode nb[] = { { 0.0f, 0.0f, 0.8f }, { 1.0f, 1.0f, 0.0f } }; bent.setNodes (nb, 2);
    REQUIRE (approx (lin.valueAt (0.5f), 0.5f, 0.01f));          // 0 curve = linear
    REQUIRE (std::abs (bent.valueAt (0.5f) - 0.5f) > 0.1f);      // bent away from the straight line
    REQUIRE (bent.valueAt (0.5f) > 0.5f);                        // +curve bends up
}

TEST_CASE ("DuckShape: curve survives toString/fromString", "[dsp][shape]")
{
    DuckShape a; ShapeNode n[] = { { 0.0f, 0.0f, 0.5f }, { 0.5f, 0.7f, -0.3f }, { 1.0f, 1.0f, 0.0f } };
    a.setNodes (n, 3);
    DuckShape b; b.fromString (a.toString());
    REQUIRE (approx (a.valueAt (0.25f), b.valueAt (0.25f), 1.0e-3f));
    REQUIRE (approx (a.valueAt (0.70f), b.valueAt (0.70f), 1.0e-3f));
}
