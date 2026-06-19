#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/TempoSync.h"
#include <memory>
#include <vector>

/**
    Central registry of parameter IDs and the APVTS layout factory — a single
    source of truth for automation, presets and state recall.
*/
namespace gf::params
{
    namespace id
    {
        constexpr auto threshold = "threshold"; // dB
        constexpr auto ratio     = "ratio";     // :1
        constexpr auto knee      = "knee";      // dB
        constexpr auto attack    = "attack";    // ms
        constexpr auto release   = "release";   // ms
        constexpr auto hold      = "hold";      // ms
        constexpr auto makeup    = "makeup";    // dB
        constexpr auto detector  = "detector";  // 0 = peak, 1 = RMS
        constexpr auto mix       = "mix";        // wet/dry (parallel), 0..1
        constexpr auto range     = "range";      // max gain reduction, dB
        constexpr auto link      = "link";       // stereo link, 0..1
        constexpr auto automakeup = "automakeup";
        constexpr auto trigger   = "trigger";   // Internal / External SC / Tempo Duck
        constexpr auto scHpf     = "schpf";     // sidechain detector high-pass, Hz
        constexpr auto scLpf     = "sclpf";     // sidechain detector low-pass, Hz
        constexpr auto scListen  = "sclisten";  // audition the detection signal
        constexpr auto duckRate  = "duckrate";   // tempo-duck note division
        constexpr auto duckDepth = "duckdepth";  // tempo-duck depth, dB
        constexpr auto duckCurve = "duckcurve";  // tempo-duck recovery curve, 0..1
        constexpr auto syncRelease = "syncrelease";
        constexpr auto releaseDiv  = "releasediv"; // tempo-synced release division
        constexpr auto gain      = "gain";      // output gain, dB
        constexpr auto bypass    = "bypass";
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;

        auto skewed = [] (float lo, float hi, float interval, float centre)
        {
            NormalisableRange<float> r (lo, hi, interval);
            r.setSkewForCentre (centre);
            return r;
        };

        auto floatParam = [] (const char* pid, const char* name,
                              NormalisableRange<float> range, float def, const char* label)
        {
            return std::make_unique<AudioParameterFloat> (
                ParameterID { pid, 1 }, name, range, def,
                AudioParameterFloatAttributes().withLabel (label));
        };

        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        p.push_back (floatParam (id::threshold, "Threshold",
                                 NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, "dB"));
        p.push_back (floatParam (id::ratio, "Ratio",
                                 skewed (1.0f, 20.0f, 0.01f, 4.0f), 4.0f, ":1"));
        p.push_back (floatParam (id::knee, "Knee",
                                 NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, "dB"));
        p.push_back (floatParam (id::attack, "Attack",
                                 skewed (0.05f, 200.0f, 0.01f, 10.0f), 10.0f, "ms"));
        p.push_back (floatParam (id::release, "Release",
                                 skewed (5.0f, 2000.0f, 0.1f, 120.0f), 120.0f, "ms"));
        p.push_back (floatParam (id::hold, "Hold",
                                 NormalisableRange<float> (0.0f, 500.0f, 0.1f), 0.0f, "ms"));
        p.push_back (floatParam (id::makeup, "Makeup",
                                 NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, "dB"));
        p.push_back (floatParam (id::detector, "Detector",
                                 NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.2f, ""));
        p.push_back (floatParam (id::mix, "Mix",
                                 NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, ""));
        p.push_back (floatParam (id::range, "Range",
                                 NormalisableRange<float> (0.0f, 60.0f, 0.1f), 60.0f, "dB"));
        p.push_back (floatParam (id::link, "Link",
                                 NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, ""));

        // Sidechain / trigger
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { id::trigger, 1 }, "Trigger",
            StringArray { "Internal", "External SC", "Tempo Duck" }, 0));
        p.push_back (floatParam (id::scHpf, "SC HPF",
                                 skewed (20.0f, 2000.0f, 1.0f, 200.0f), 20.0f, "Hz"));
        p.push_back (floatParam (id::scLpf, "SC LPF",
                                 skewed (200.0f, 20000.0f, 1.0f, 2000.0f), 20000.0f, "Hz"));

        // Tempo-synced ducking + synced release
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { id::duckRate, 1 }, "Duck Rate", gf::dsp::divisionChoices(), 2)); // 1/4
        p.push_back (floatParam (id::duckDepth, "Duck Depth",
                                 NormalisableRange<float> (0.0f, 36.0f, 0.1f), 12.0f, "dB"));
        p.push_back (floatParam (id::duckCurve, "Duck Curve",
                                 NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, ""));
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { id::releaseDiv, 1 }, "Release Div", gf::dsp::divisionChoices(), 3)); // 1/8

        p.push_back (floatParam (id::gain, "Output",
                                 NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, "dB"));

        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { id::automakeup, 1 }, "Auto Makeup", false));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { id::scListen, 1 }, "SC Listen", false));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { id::syncRelease, 1 }, "Sync Release", false));
        p.push_back (std::make_unique<AudioParameterBool> (
            ParameterID { id::bypass, 1 }, "Bypass", false));

        return { p.begin(), p.end() };
    }
}
