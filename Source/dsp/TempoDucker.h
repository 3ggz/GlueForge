#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "TempoSync.h"

#include <cmath>

namespace gf::dsp
{
    /**
        Tempo-synced ducking envelope — the "fake sidechain" / volume-shaper pump.

        Produces a per-sample gain that dips to `minGain` on the downbeat (phase 0)
        and recovers toward 1 over the note division, following a power curve
        (exponent > 1 = pumpier / late recovery; < 1 = quick recovery):

            gain(phase) = minGain + (1 - minGain) * phase^curve

        The phase free-runs per sample (`processSample`) and is re-locked to the
        host transport at each block start (`syncToPpq`) so the pump stays smooth
        and in time across loops/relocates. Pure w.r.t. (phase, depth, curve) —
        `gainForPhase` is the unit-testable shape; the processor supplies tempo +
        position via thin adapters.
    */
    class TempoDucker
    {
    public:
        void prepare (double sampleRate) { sr_ = sampleRate > 0.0 ? sampleRate : 44100.0; phase_ = 0.0f; }
        void reset()                     { phase_ = 0.0f; }

        void setParameters (float depthDb, float curveExp)
        {
            minGain_ = juce::Decibels::decibelsToGain (-juce::jmax (0.0f, depthDb));
            curve_   = juce::jmax (0.05f, curveExp);
        }

        void setRate (double bpm, double divisionInBeats)
        {
            const double cycleSecs = divisionInBeats * (60.0 / juce::jmax (1.0e-6, bpm));
            phaseInc_ = cycleSecs > 0.0 ? (1.0 / (cycleSecs * sr_)) : 0.0;
        }

        void syncToPpq (double ppqPosition, double divisionInBeats)
        {
            if (divisionInBeats > 0.0)
            {
                const double cycles = ppqPosition / divisionInBeats;
                phase_ = (float) (cycles - std::floor (cycles));
            }
        }

        float gainForPhase (float phase) const
        {
            const float p = phase - std::floor (phase);
            return minGain_ + (1.0f - minGain_) * std::pow (p, curve_);
        }

        float processSample()
        {
            const float g = gainForPhase (phase_);
            phase_ += (float) phaseInc_;
            if (phase_ >= 1.0f) phase_ -= std::floor (phase_);
            return g;
        }

    private:
        double sr_       = 44100.0;
        double phaseInc_ = 0.0;
        float  phase_    = 0.0f;
        float  minGain_  = 0.0f;
        float  curve_    = 2.0f;
    };
}
