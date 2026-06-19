#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "GainComputer.h"
#include "BallisticsSmoother.h"
#include "LevelDetector.h"

namespace gf::dsp
{
    struct CompressorParameters
    {
        float thresholdDb    = 0.0f;
        float ratio          = 1.0f;
        float kneeDb         = 0.0f;
        float attackMs       = 10.0f;
        float releaseMs      = 100.0f;
        float holdMs         = 0.0f;
        float makeupDb       = 0.0f;
        float detectorBlend  = 0.0f;  // 0 = peak, 1 = RMS

        bool operator== (const CompressorParameters& o) const
        {
            return thresholdDb == o.thresholdDb && ratio == o.ratio && kneeDb == o.kneeDb
                && attackMs == o.attackMs && releaseMs == o.releaseMs && holdMs == o.holdMs
                && makeupDb == o.makeupDb && detectorBlend == o.detectorBlend;
        }
        bool operator!= (const CompressorParameters& o) const { return ! (*this == o); }
    };

    /**
        One feed-forward compressor instance (the reusable unit — a multiband band
        is just one of these). Per sample:

            detect level (linked across channels) -> dB
              -> static curve (GainComputer) -> target gain reduction (dB)
              -> attack/release/hold ballistics -> smoothed GR (dB)
              -> gain = dB->lin(GR) * makeup, applied to all channels

        getGainReductionDb() returns the peak (most negative) smoothed reduction of
        the last processed block, for the GR meter. Makeup is smoothed so parameter
        moves don't zipper; threshold/ratio/knee changes are absorbed by the
        ballistics (the target GR moves, the smoother glides to it).

        Real-time safe: no allocation/locks; all state lives here, set in prepare().
    */
    class Compressor
    {
    public:
        void prepare (double sampleRate, int numChannels)
        {
            sr_    = sampleRate > 0.0 ? sampleRate : 44100.0;
            numCh_ = numChannels;

            detector_.prepare (sr_);
            ballistics_.prepare (sr_);
            makeupGain_.reset (sr_, 0.02);
            makeupGain_.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params_.makeupDb));

            reset();
        }

        void setParameters (const CompressorParameters& p)
        {
            params_ = p;
            gainComputer_.setParameters (p.thresholdDb, p.ratio, p.kneeDb);
            ballistics_.setTimes (p.attackMs, p.releaseMs, p.holdMs);
            detector_.setBlend (p.detectorBlend);
            makeupGain_.setTargetValue (juce::Decibels::decibelsToGain (p.makeupDb));
        }

        void reset()
        {
            detector_.reset();
            ballistics_.reset (0.0f);
            grMeterDb_ = 0.0f;
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            const int n  = buffer.getNumSamples();
            const int ch = juce::jmin (numCh_, buffer.getNumChannels());
            if (ch <= 0 || n <= 0)
                return;

            auto* const* chans = buffer.getArrayOfWritePointers();

            float worst = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                // Linked detection from the loudest channel.
                float det = 0.0f;
                for (int c = 0; c < ch; ++c)
                    det = juce::jmax (det, std::abs (chans[c][i]));

                const float lvlLin   = detector_.processSample (det);
                const float lvlDb    = juce::Decibels::gainToDecibels (lvlLin, -100.0f);
                const float targetGr = gainComputer_.computeGainReductionDb (lvlDb);
                const float grDb     = ballistics_.processSample (targetGr);

                const float gain = juce::Decibels::decibelsToGain (grDb) * makeupGain_.getNextValue();
                for (int c = 0; c < ch; ++c)
                    chans[c][i] *= gain;

                worst = juce::jmin (worst, grDb);
            }

            grMeterDb_ = worst;
        }

        float getGainReductionDb() const { return grMeterDb_; }

    private:
        double sr_    = 44100.0;
        int    numCh_ = 2;

        CompressorParameters params_;
        LevelDetector        detector_;
        GainComputer         gainComputer_;
        BallisticsSmoother   ballistics_;
        juce::LinearSmoothedValue<float> makeupGain_;

        float grMeterDb_ = 0.0f;
    };
}
