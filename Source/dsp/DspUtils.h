#pragma once

#include <cmath>

namespace gf::dsp
{
    /** One-pole smoothing coefficient from a time constant in ms and the sample
        rate. Reaches ~63% of a step after `ms`. Returns 0 (instant) for ms <= 0. */
    inline float onePoleCoeff (float ms, double sampleRate)
    {
        if (ms <= 0.0f) return 0.0f;
        return (float) std::exp (-1.0 / ((double) ms * 0.001 * sampleRate));
    }
}
