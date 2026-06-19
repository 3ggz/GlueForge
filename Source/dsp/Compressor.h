#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "GainComputer.h"
#include "BallisticsSmoother.h"
#include "LevelDetector.h"

#include <array>

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
        float rangeDb        = 60.0f; // max gain reduction allowed (dB); large = effectively off
        float stereoLink     = 1.0f;  // 0 = unlinked (per channel), 1 = fully linked
        bool  autoMakeup     = false;

        bool operator== (const CompressorParameters& o) const
        {
            return thresholdDb == o.thresholdDb && ratio == o.ratio && kneeDb == o.kneeDb
                && attackMs == o.attackMs && releaseMs == o.releaseMs && holdMs == o.holdMs
                && makeupDb == o.makeupDb && detectorBlend == o.detectorBlend
                && rangeDb == o.rangeDb && stereoLink == o.stereoLink && autoMakeup == o.autoMakeup;
        }
        bool operator!= (const CompressorParameters& o) const { return ! (*this == o); }
    };

    /**
        One feed-forward compressor instance (the reusable unit — a multiband band
        is just one of these). Per sample, for each channel:

            detect level (per channel) -> blend toward the linked/loudest level
              -> dB -> static curve (GainComputer) -> target gain reduction (dB)
              -> clamp to the range (max reduction) -> attack/release/hold ballistics
              -> gain = dB->lin(GR) * makeup, applied in place.

        Detection and ballistics are per channel so the stereo-link amount can vary
        continuously from fully linked (the louder channel drives both) to unlinked
        (independent). Makeup can be manual or auto (compensating the curve's
        reduction at 0 dBFS). getGainReductionDb() is the peak (most negative)
        reduction of the last block, for the GR meter.

        Real-time safe: no allocation/locks; all state set in prepare().
    */
    class Compressor
    {
    public:
        static constexpr int kMaxChannels = 2;

        void prepare (double sampleRate, int numChannels)
        {
            sr_    = sampleRate > 0.0 ? sampleRate : 44100.0;
            numCh_ = juce::jlimit (1, kMaxChannels, numChannels);

            for (auto& d : detector_)   d.prepare (sr_);
            for (auto& b : ballistics_) b.prepare (sr_);

            makeupGain_.reset (sr_, 0.02);
            makeupGain_.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params_.makeupDb));

            reset();
        }

        void setParameters (const CompressorParameters& p)
        {
            params_ = p;

            gainComputer_.setParameters (p.thresholdDb, p.ratio, p.kneeDb);
            for (auto& d : detector_)   d.setBlend (p.detectorBlend);
            for (auto& b : ballistics_) b.setTimes (p.attackMs, p.releaseMs, p.holdMs);

            rangeDb_ = p.rangeDb < 0.0f ? 0.0f : p.rangeDb;
            link_    = juce::jlimit (0.0f, 1.0f, p.stereoLink);

            const float mk = p.autoMakeup ? -gainComputer_.computeGainReductionDb (0.0f)
                                          : p.makeupDb;
            effectiveMakeupDb_ = mk;
            makeupGain_.setTargetValue (juce::Decibels::decibelsToGain (mk));
        }

        void reset()
        {
            for (auto& d : detector_)   d.reset();
            for (auto& b : ballistics_) b.reset (0.0f);
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
                float lvl[kMaxChannels];
                float maxLvl = 0.0f;
                for (int c = 0; c < ch; ++c)
                {
                    lvl[c] = detector_[c].processSample (chans[c][i]);
                    maxLvl = juce::jmax (maxLvl, lvl[c]);
                }

                const float mk = makeupGain_.getNextValue(); // once per sample

                for (int c = 0; c < ch; ++c)
                {
                    const float linked   = link_ * maxLvl + (1.0f - link_) * lvl[c];
                    const float lvlDb    = juce::Decibels::gainToDecibels (linked, -100.0f);
                    float       targetGr = gainComputer_.computeGainReductionDb (lvlDb);
                    if (targetGr < -rangeDb_) targetGr = -rangeDb_; // range cap

                    const float grDb = ballistics_[c].processSample (targetGr);
                    chans[c][i] *= juce::Decibels::decibelsToGain (grDb) * mk;
                    worst = juce::jmin (worst, grDb);
                }
            }

            grMeterDb_ = worst;
        }

        float getGainReductionDb() const { return grMeterDb_; }
        float getMakeupDb()        const { return effectiveMakeupDb_; }

    private:
        double sr_    = 44100.0;
        int    numCh_ = 2;

        CompressorParameters params_;
        std::array<LevelDetector, kMaxChannels>      detector_;
        std::array<BallisticsSmoother, kMaxChannels> ballistics_;
        GainComputer                                 gainComputer_;
        juce::LinearSmoothedValue<float>             makeupGain_;

        float rangeDb_           = 60.0f;
        float link_              = 1.0f;
        float effectiveMakeupDb_ = 0.0f;
        float grMeterDb_         = 0.0f;
    };
}
