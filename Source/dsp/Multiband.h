#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Compressor.h"

#include <array>

namespace gf::dsp
{
    /**
        3-band multiband compressor. Splits with Linkwitz-Riley (LR4) crossovers
        into low / mid / high; the low band is run through an allpass at the upper
        crossover so the three bands sum back flat (allpass reconstruction). Each
        band is an independent Compressor instance (the reusable unit), detecting
        from its own band-split key. Per-band output trim, bypass, and solo.

        Real-time safe: band buffers preallocated in prepare(); processing wraps
        them as views (no allocation on the audio thread).
    */
    class Multiband
    {
    public:
        static constexpr int kBands = 3;

        void prepare (double sampleRate, int numChannels, int maxBlock)
        {
            sr_    = sampleRate;
            numCh_ = numChannels;

            juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) juce::jmax (1, numChannels) };
            for (auto* f : { &xo1_, &xo2_, &dXo1_, &dXo2_, &apLow_, &dApLow_ })
                f->prepare (spec);

            xo1_.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
            xo2_.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
            dXo1_.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            dXo2_.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            apLow_.setType  (juce::dsp::LinkwitzRileyFilterType::allpass);
            dApLow_.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

            for (auto& c : comp_) c.prepare (sampleRate, numChannels);
            for (auto& b : bandMain_) b.setSize (numChannels, maxBlock, false, false, true);
            for (auto& b : bandDet_)  b.setSize (numChannels, maxBlock, false, false, true);

            setCrossovers (200.0f, 2000.0f);
            reset();
        }

        void setCrossovers (float fLowMid, float fMidHigh)
        {
            const float nyq = (float) (sr_ * 0.49);
            fLowMid  = juce::jlimit (30.0f, nyq, fLowMid);
            fMidHigh = juce::jlimit (fLowMid + 1.0f, nyq, fMidHigh);
            xo1_.setCutoffFrequency (fLowMid);  dXo1_.setCutoffFrequency (fLowMid);
            xo2_.setCutoffFrequency (fMidHigh); dXo2_.setCutoffFrequency (fMidHigh);
            apLow_.setCutoffFrequency (fMidHigh); dApLow_.setCutoffFrequency (fMidHigh);
        }

        // Per-band parameters (each band is an independent compressor).
        void setBandParams (int band, const CompressorParameters& p)
        {
            if (juce::isPositiveAndBelow (band, kBands)) comp_[(size_t) band].setParameters (p);
        }

        // Convenience: same params on every band.
        void setCompressorParams (const CompressorParameters& p)
        {
            for (auto& c : comp_) c.setParameters (p);
        }

        float getBandGainReductionDb (int band) const
        {
            return bandGr_[(size_t) juce::jlimit (0, kBands - 1, band)];
        }

        void setBand (int i, float trimDb, bool bypassed)
        {
            if (juce::isPositiveAndBelow (i, kBands))
            {
                trimGain_[(size_t) i] = juce::Decibels::decibelsToGain (trimDb);
                bypass_[(size_t) i]   = bypassed;
            }
        }

        void setSolo (int soloBand) { solo_ = soloBand; } // -1 none, else 0..2

        void reset()
        {
            for (auto* f : { &xo1_, &xo2_, &dXo1_, &dXo2_, &apLow_, &dApLow_ }) f->reset();
            for (auto& c : comp_) c.reset();
            grMeterDb_ = 0.0f;
        }

        void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& detection)
        {
            const int n  = main.getNumSamples();
            const int ch = juce::jmin (numCh_, main.getNumChannels());
            if (ch <= 0 || n <= 0) return;
            const int detCh = juce::jmax (1, detection.getNumChannels());

            // Split main + detection into 3 bands (allpass-compensated low band).
            for (int c = 0; c < ch; ++c)
            {
                const auto* m = main.getReadPointer (c);
                const auto* d = detection.getReadPointer (juce::jmin (c, detCh - 1));
                for (int i = 0; i < n; ++i)
                {
                    float low, rest, mid, high;
                    xo1_.processSample (c, m[i], low, rest);
                    xo2_.processSample (c, rest, mid, high);
                    low = apLow_.processSample (c, low);
                    bandMain_[0].setSample (c, i, low);
                    bandMain_[1].setSample (c, i, mid);
                    bandMain_[2].setSample (c, i, high);

                    dXo1_.processSample (c, d[i], low, rest);
                    dXo2_.processSample (c, rest, mid, high);
                    low = dApLow_.processSample (c, low);
                    bandDet_[0].setSample (c, i, low);
                    bandDet_[1].setSample (c, i, mid);
                    bandDet_[2].setSample (c, i, high);
                }
            }

            // Compress each (non-bypassed) band from its own band key.
            float worst = 0.0f;
            for (int b = 0; b < kBands; ++b)
            {
                if (bypass_[(size_t) b]) { bandGr_[(size_t) b] = 0.0f; continue; }
                juce::AudioBuffer<float> bm (bandMain_[(size_t) b].getArrayOfWritePointers(), ch, n);
                juce::AudioBuffer<float> bd (bandDet_[(size_t) b].getArrayOfWritePointers(),  ch, n);
                comp_[(size_t) b].process (bm, &bd);
                bandGr_[(size_t) b] = comp_[(size_t) b].getGainReductionDb();
                worst = juce::jmin (worst, bandGr_[(size_t) b]);
            }
            grMeterDb_ = worst;

            // Recombine (respecting solo + per-band trim).
            main.clear();
            for (int b = 0; b < kBands; ++b)
            {
                if (solo_ >= 0 && solo_ != b) continue;
                for (int c = 0; c < ch; ++c)
                    main.addFrom (c, 0, bandMain_[(size_t) b].getReadPointer (c), n, trimGain_[(size_t) b]);
            }
        }

        float getGainReductionDb() const { return grMeterDb_; }

    private:
        double sr_    = 44100.0;
        int    numCh_ = 2;

        juce::dsp::LinkwitzRileyFilter<float> xo1_, xo2_, apLow_;     // main
        juce::dsp::LinkwitzRileyFilter<float> dXo1_, dXo2_, dApLow_;  // detection
        std::array<Compressor, kBands> comp_;
        std::array<juce::AudioBuffer<float>, kBands> bandMain_, bandDet_;

        std::array<float, kBands> trimGain_ { { 1.0f, 1.0f, 1.0f } };
        std::array<bool,  kBands> bypass_   { { false, false, false } };
        int   solo_      = -1;
        float grMeterDb_ = 0.0f;
        std::array<float, kBands> bandGr_ { { 0.0f, 0.0f, 0.0f } };
    };
}
