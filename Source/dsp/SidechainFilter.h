#pragma once

#include <juce_dsp/juce_dsp.h>

namespace gf::dsp
{
    /**
        Detector-path filter: a high-pass + low-pass (12 dB/oct each) shaping what
        the compressor reacts to — e.g. high-pass so a bass-bus comp ignores the
        sub and reacts to the kick fundamental.

        Bypasses each stage at its extreme setting (HP <= ~20 Hz, LP >= ~19 kHz) so
        the default is a true pass-through (DC / sub content survives untouched).
        Per-sample, per-channel API so it sits in the detector loop.
    */
    class SidechainFilter
    {
    public:
        void prepare (double sampleRate, int numChannels)
        {
            sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;

            juce::dsp::ProcessSpec spec;
            spec.sampleRate       = sr_;
            spec.maximumBlockSize = 4096;
            spec.numChannels      = (juce::uint32) juce::jmax (1, numChannels);

            hp_.prepare (spec);
            lp_.prepare (spec);
            hp_.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            lp_.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

            setCutoffs (20.0f, 20000.0f);
            reset();
        }

        void setCutoffs (float hpHz, float lpHz)
        {
            const float nyq = (float) (sr_ * 0.45);
            hpActive_ = hpHz > 21.0f;
            lpActive_ = lpHz < 19000.0f;
            if (hpActive_) hp_.setCutoffFrequency (juce::jlimit (10.0f, nyq, hpHz));
            if (lpActive_) lp_.setCutoffFrequency (juce::jlimit (10.0f, nyq, lpHz));
        }

        bool isActive() const { return hpActive_ || lpActive_; }

        void reset() { hp_.reset(); lp_.reset(); }

        float processSample (int channel, float x)
        {
            if (hpActive_) x = hp_.processSample (channel, x);
            if (lpActive_) x = lp_.processSample (channel, x);
            return x;
        }

    private:
        double sr_ = 44100.0;
        bool hpActive_ = false, lpActive_ = false;
        juce::dsp::StateVariableTPTFilter<float> hp_, lp_;
    };
}
