#pragma once

#include "TempoSync.h"
#include <cmath>

namespace gf::dsp
{
    /**
        Tempo-synced phase clock for the pump/duck. Free-runs at the host tempo and
        re-locks to the transport at each block start; the gain envelope itself is
        defined by an editable DuckShape, evaluated by the processor at the phase
        this clock provides. Pure and allocation-free.
    */
    class TempoDucker
    {
    public:
        void prepare (double sampleRate) { sr_ = sampleRate > 0.0 ? sampleRate : 44100.0; phase_ = 0.0f; }
        void reset()                     { phase_ = 0.0f; }

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

        // Returns the current phase in [0,1), then advances by one sample.
        float advance()
        {
            const float p = phase_;
            phase_ += (float) phaseInc_;
            if (phase_ >= 1.0f) phase_ -= std::floor (phase_);
            return p;
        }

        float currentPhase() const { return phase_; }

    private:
        double sr_       = 44100.0;
        double phaseInc_ = 0.0;
        float  phase_    = 0.0f;
    };
}
