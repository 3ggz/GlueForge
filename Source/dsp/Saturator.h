#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace gf::dsp
{
    enum class CharacterModel { VCA = 0, FET = 1, Opto = 2 };

    // Per-model timing character (multiplies the user's attack/release). FET is
    // snappier, Opto smoother — VCA neutral. Modest, so it's a flavour not a lie.
    inline float characterAttackScale (CharacterModel m)
    {
        switch (m) { case CharacterModel::FET: return 0.6f; case CharacterModel::Opto: return 1.6f; default: return 1.0f; }
    }
    inline float characterReleaseScale (CharacterModel m)
    {
        switch (m) { case CharacterModel::FET: return 0.7f; case CharacterModel::Opto: return 1.5f; default: return 1.0f; }
    }

    /**
        Harmonic-saturation / coloration stage, one waveshape per character model:
          - VCA  : symmetric tanh — clean, odd harmonics, tight.
          - FET  : harder drive + asymmetry — aggressive, even+odd (1176 attitude).
          - Opto : gentle drive + slight asymmetry — warm, subtle.

        `drive` (0..1) sets intensity; `mix` (0..1) blends in the colored signal
        (0 = clean). Small-signal gain is ~unity (the shaper is `tanh(g·x)/g` with a
        model bias for asymmetry), so dialing drive doesn't change level much until
        it bites. Stateless waveshaper — RT-safe, nothing to allocate.
    */
    class Saturator
    {
    public:
        void prepare (double, int) {}
        void reset() {}

        void setModel (CharacterModel m) { model_ = m; }
        void setDrive (float d)          { drive_ = juce::jlimit (0.0f, 1.0f, d); }
        void setMix   (float m)          { mix_   = juce::jlimit (0.0f, 1.0f, m); }

        void process (juce::AudioBuffer<float>& buffer)
        {
            process (juce::dsp::AudioBlock<float> (buffer));
        }

        void process (juce::dsp::AudioBlock<float> block)
        {
            if (mix_ <= 0.0f) return; // fully clean — skip

            const size_t nc = block.getNumChannels();
            const size_t ns = block.getNumSamples();
            for (size_t c = 0; c < nc; ++c)
            {
                auto* d = block.getChannelPointer (c);
                for (size_t i = 0; i < ns; ++i)
                {
                    const float x = d[i];
                    d[i] = mix_ * saturateSample (model_, x, drive_) + (1.0f - mix_) * x;
                }
            }
        }

        static float saturateSample (CharacterModel m, float x, float drive01)
        {
            float scale, bias;
            switch (m)
            {
                case CharacterModel::FET:  scale = 16.0f; bias = 0.3f; break;
                case CharacterModel::Opto: scale =  4.0f; bias = 0.1f; break;
                default:                   scale =  8.0f; bias = 0.0f; break; // VCA
            }
            const float g = 1.0f + drive01 * scale;
            return (std::tanh (g * x + bias) - std::tanh (bias)) / g;
        }

    private:
        CharacterModel model_ = CharacterModel::VCA;
        float drive_ = 0.0f;
        float mix_   = 0.0f;
    };
}
