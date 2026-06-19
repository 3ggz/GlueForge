#pragma once

#include <cmath>

namespace gf::dsp
{
    /**
        Attack / release / hold ballistics applied to the gain-reduction signal
        (in the log / dB domain).

        The smoother is a one-pole moving toward the target gain reduction:
          - when the target reduction *deepens* (more negative than current) it
            uses the attack coefficient, and arms the hold counter;
          - when the target reduction *eases* (less negative) it first holds for
            the hold time, then releases with the release coefficient.

        Coefficients are derived from time-in-ms x sample-rate, so behaviour is
        sample-rate independent. A one-pole reaches ~63% of a step after one time
        constant. time = 0 ms gives an instantaneous (coefficient 0) response.
    */
    class BallisticsSmoother
    {
    public:
        void prepare (double sampleRate)
        {
            sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
            recompute();
        }

        void setTimes (float attackMs, float releaseMs, float holdMs)
        {
            attackMs_  = attackMs;
            releaseMs_ = releaseMs;
            holdMs_    = holdMs;
            recompute();
        }

        void reset (float initialGrDb = 0.0f)
        {
            current_     = initialGrDb;
            holdCounter_ = 0;
        }

        float processSample (float targetGrDb)
        {
            if (targetGrDb < current_)
            {
                // deepening reduction -> attack
                current_     = targetGrDb + attackCoeff_ * (current_ - targetGrDb);
                holdCounter_ = holdSamples_;
            }
            else
            {
                // easing reduction -> hold, then release
                if (holdCounter_ > 0)
                    --holdCounter_;
                else
                    current_ = targetGrDb + releaseCoeff_ * (current_ - targetGrDb);
            }

            return current_;
        }

        float getCurrent() const { return current_; }

    private:
        static float coeff (float ms, double sr)
        {
            if (ms <= 0.0f) return 0.0f;
            return (float) std::exp (-1.0 / ((double) ms * 0.001 * sr));
        }

        void recompute()
        {
            attackCoeff_  = coeff (attackMs_,  sr_);
            releaseCoeff_ = coeff (releaseMs_, sr_);
            holdSamples_  = (int) std::lround ((double) holdMs_ * 0.001 * sr_);
        }

        double sr_ = 44100.0;
        float attackMs_ = 10.0f, releaseMs_ = 100.0f, holdMs_ = 0.0f;
        float attackCoeff_ = 0.0f, releaseCoeff_ = 0.0f;
        int   holdSamples_ = 0, holdCounter_ = 0;
        float current_ = 0.0f;
    };
}
