#pragma once

namespace gf::dsp
{
    /**
        Static compressor transfer curve.

        Pure function of input level: given a level in dB, returns the gain
        reduction in dB (<= 0) for the configured threshold, ratio and (soft) knee.
        Uses the canonical soft-knee formulation (Reiss/McPherson):

            below knee  (overshoot <= -W/2):  GR = 0
            within knee (-W/2 < o < +W/2):     GR = (1/R - 1) * (o + W/2)^2 / (2W)
            above knee  (overshoot >= +W/2):   GR = (1/R - 1) * overshoot

        where overshoot = inputDb - threshold, W = knee width. Hard knee is W = 0.
    */
    class GainComputer
    {
    public:
        void setParameters (float thresholdDb, float ratio, float kneeWidthDb)
        {
            thresholdDb_ = thresholdDb;
            ratio_       = ratio < 1.0f ? 1.0f : ratio;
            kneeDb_      = kneeWidthDb < 0.0f ? 0.0f : kneeWidthDb;
            slope_       = 1.0f / ratio_ - 1.0f; // <= 0
        }

        float computeGainReductionDb (float inputLevelDb) const
        {
            const float overshoot = inputLevelDb - thresholdDb_;

            if (kneeDb_ <= 0.0f)
                return overshoot <= 0.0f ? 0.0f : slope_ * overshoot;

            const float halfKnee = 0.5f * kneeDb_;

            if (overshoot <= -halfKnee) return 0.0f;
            if (overshoot >=  halfKnee) return slope_ * overshoot;

            const float x = overshoot + halfKnee;
            return slope_ * (x * x) / (2.0f * kneeDb_);
        }

        float getThresholdDb() const { return thresholdDb_; }
        float getRatio()       const { return ratio_; }
        float getKneeDb()      const { return kneeDb_; }

    private:
        float thresholdDb_ = 0.0f;
        float ratio_       = 1.0f;
        float kneeDb_      = 0.0f;
        float slope_       = 0.0f;
    };
}
