#pragma once

#include <cmath>

namespace gf::dsp
{
    /**
        Input level detector with a Peak <-> RMS blend.

          - Peak: rectified signal through a follower with instantaneous attack and
            an exponential release.
          - RMS: a one-pole average of the squared signal; sqrt gives the level.
          - Blend: linear interpolation between the two level estimates
            (0 = pure peak, 1 = pure RMS).

        For a steady sine of amplitude A the peak path reports ~A and the RMS path
        reports A/sqrt(2) (-3.01 dB). For a constant-magnitude signal both report A.
        Input may be signed; it is rectified internally.
    */
    class LevelDetector
    {
    public:
        void prepare (double sampleRate)
        {
            sr_           = sampleRate > 0.0 ? sampleRate : 44100.0;
            peakRelCoeff_ = onePole (peakReleaseMs_, sr_);
            rmsCoeff_     = onePole (rmsWindowMs_,   sr_);
            reset();
        }

        void setBlend (float blend01)
        {
            blend_ = blend01 < 0.0f ? 0.0f : (blend01 > 1.0f ? 1.0f : blend01);
        }

        void reset()
        {
            peakEnv_ = 0.0f;
            msEnv_   = 0.0f;
        }

        float processSample (float x)
        {
            const float r = std::abs (x);

            // Peak follower: instant attack, exponential release.
            if (r > peakEnv_) peakEnv_ = r;
            else              peakEnv_ = r + peakRelCoeff_ * (peakEnv_ - r);

            // RMS follower: one-pole on the squared signal.
            const float sq = r * r;
            msEnv_ = sq + rmsCoeff_ * (msEnv_ - sq);
            const float rms = std::sqrt (msEnv_ < 0.0f ? 0.0f : msEnv_);

            return peakEnv_ + blend_ * (rms - peakEnv_);
        }

    private:
        static float onePole (float ms, double sr)
        {
            if (ms <= 0.0f) return 0.0f;
            return (float) std::exp (-1.0 / ((double) ms * 0.001 * sr));
        }

        double sr_ = 44100.0;
        float blend_ = 0.0f;
        float peakReleaseMs_ = 50.0f, rmsWindowMs_ = 10.0f;
        float peakRelCoeff_  = 0.0f,  rmsCoeff_    = 0.0f;
        float peakEnv_ = 0.0f, msEnv_ = 0.0f;
    };
}
