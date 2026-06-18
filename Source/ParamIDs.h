#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

/**
    Central registry of parameter IDs and the APVTS layout factory.

    Keeping every parameter ID in one place (rather than scattering string
    literals) means automation, presets and state recall all refer to a single
    source of truth. New parameters in later phases are added here.
*/
namespace gf::params
{
    namespace id
    {
        constexpr auto gain   = "gain";    // output/trim gain in dB (Phase 1)
        constexpr auto bypass = "bypass";  // soft bypass
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;

        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        params.push_back(std::make_unique<AudioParameterFloat>(
            ParameterID { id::gain, 1 }, "Gain",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back(std::make_unique<AudioParameterBool>(
            ParameterID { id::bypass, 1 }, "Bypass", false));

        return { params.begin(), params.end() };
    }
}
