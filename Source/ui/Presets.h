#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../ParamIDs.h"
#include <vector>
#include <utility>

namespace gf::ui
{
    struct Preset
    {
        juce::String name;
        juce::String category;
        std::vector<std::pair<juce::String, float>> values; // paramID -> real value
    };

    inline std::vector<Preset> factoryPresets()
    {
        using namespace gf::params::id;
        // trigger: 0 Internal, 1 External SC, 2 Tempo Duck.  character: 0 VCA, 1 FET, 2 Opto.
        return {
            { "Init", "Basic",
              { {threshold,-18},{ratio,4},{knee,6},{attack,10},{release,120},{hold,0},{makeup,0},
                {detector,0.2f},{mix,1},{range,60},{link,1},{automakeup,0},{trigger,0},
                {scHpf,20},{scLpf,20000},{character,0},{drive,0.3f},{satMix,0},{gain,0} } },

            { "Bus Glue (SSL)", "Bus Glue",
              { {threshold,-16},{ratio,2},{knee,6},{attack,30},{release,150},{detector,0.4f},
                {mix,1},{link,1},{automakeup,1},{trigger,0},{character,0},{satMix,0} } },

            { "Master Glue", "Master",
              { {threshold,-12},{ratio,1.5f},{knee,10},{attack,30},{release,300},{detector,0.5f},
                {range,6},{mix,1},{link,1},{automakeup,1},{trigger,0},{character,0},{drive,0.2f},{satMix,0.15f} } },

            { "Vocal Leveler (Opto)", "Vocal",
              { {threshold,-24},{ratio,3},{knee,8},{attack,15},{release,200},{detector,0.6f},
                {mix,1},{automakeup,1},{trigger,0},{character,2},{satMix,0} } },

            { "Bass Control", "Bass",
              { {threshold,-20},{ratio,4},{knee,6},{attack,20},{release,150},{detector,0.4f},
                {scHpf,80},{mix,1},{trigger,0},{character,2},{drive,0.3f},{satMix,0.1f} } },

            { "Drum Bus Smash (FET)", "Drums",
              { {threshold,-20},{ratio,8},{knee,0},{attack,5},{release,80},{detector,0.0f},
                {mix,1},{trigger,0},{character,1},{drive,0.5f},{satMix,0.3f} } },

            { "Parallel Smash (NY)", "Parallel",
              { {threshold,-35},{ratio,10},{knee,0},{attack,1},{release,60},{detector,0.0f},
                {mix,0.4f},{trigger,0},{character,1},{drive,0.6f},{satMix,0.4f} } },

            { "EDM Sidechain Pump (Bass)", "EDM Sidechain",
              { {threshold,-30},{ratio,8},{knee,2},{attack,1},{release,140},{detector,0.0f},
                {scHpf,40},{mix,1},{link,1},{trigger,1},{character,0},{satMix,0} } },

            { "Tempo Duck 1/4 (Drop Pump)", "EDM Sidechain",
              { {trigger,2},{duckRate,2},{duckDepth,20},{duckCurve,0.35f},{mix,1},{gain,0} } },

            { "Chord Stab Duck 1/8", "EDM Sidechain",
              { {trigger,2},{duckRate,3},{duckDepth,14},{duckCurve,0.5f},{mix,1},{gain,0} } },
        };
    }

    inline void applyPreset (juce::AudioProcessorValueTreeState& apvts, const Preset& p)
    {
        for (auto& kv : p.values)
            if (auto* prm = apvts.getParameter (kv.first))
                prm->setValueNotifyingHost (prm->convertTo0to1 (kv.second));
    }
}
