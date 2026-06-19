#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LookAndFeel.h"
#include "../dsp/GainComputer.h"

#include <array>

namespace gf::ui
{
    /** Vertical peak level meter (-60..0 dB), instant rise / smooth fall. */
    class LevelMeter : public juce::Component
    {
    public:
        void setLevelDb (float db)
        {
            if (db > display_) display_ = db;                 // instant attack
            else               display_ = display_ * 0.82f + db * 0.18f; // smooth release
        }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::track); g.fillRoundedRectangle (b, 2.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (display_, -60.0f, 0.0f, 0.0f, 1.0f));
            auto fill = b.withTop (b.getBottom() - b.getHeight() * norm);
            g.setColour (display_ > -0.5f ? juce::Colour (0xffe2575b) : colours::accent);
            g.fillRoundedRectangle (fill, 2.0f);
        }
    private:
        float display_ = -100.0f;
    };

    /** Gain-reduction meter — grows downward from the top (0..30 dB). */
    class GrMeter : public juce::Component
    {
    public:
        void setReductionDb (float grDb)
        {
            const float mag = -grDb;
            if (mag > display_) display_ = mag;
            else                display_ = display_ * 0.85f + mag * 0.15f;
        }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::track); g.fillRoundedRectangle (b, 2.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, display_ / 30.0f);
            g.setColour (colours::accent2);
            g.fillRoundedRectangle (b.withHeight (b.getHeight() * norm), 2.0f);
        }
    private:
        float display_ = 0.0f;
    };

    /** Static transfer curve (threshold/ratio/knee) with a live operating point. */
    class TransferCurveComponent : public juce::Component
    {
    public:
        void setParams (float t, float r, float k) { thr_ = t; ratio_ = r; knee_ = k; }
        void setOperatingPoint (float inDb, float grDb) { opIn_ = inDb; opGr_ = grDb; }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (colours::panel); g.fillRoundedRectangle (b, 3.0f);

            g.setColour (colours::track.withAlpha (0.6f));
            for (int dB = -48; dB <= 0; dB += 12)
            {
                const float x = juce::jmap ((float) dB, -60.0f, 0.0f, b.getX(), b.getRight());
                const float y = juce::jmap ((float) dB, -60.0f, 0.0f, b.getBottom(), b.getY());
                g.drawVerticalLine   ((int) x, b.getY(), b.getBottom());
                g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
            }

            // unity diagonal
            g.setColour (colours::dim.withAlpha (0.5f));
            g.drawLine ({ toXY (-60.0f, -60.0f, b), toXY (0.0f, 0.0f, b) }, 1.0f);

            gf::dsp::GainComputer gc; gc.setParameters (thr_, ratio_, knee_);
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const float inDb  = -60.0f + (float) i * 0.5f;
                const float outDb = inDb + gc.computeGainReductionDb (inDb);
                const auto pt = toXY (inDb, outDb, b);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (colours::accent);
            g.strokePath (p, juce::PathStrokeType (2.0f));

            if (opIn_ > -90.0f)
            {
                const auto pt = toXY (opIn_, opIn_ + opGr_, b);
                g.setColour (colours::accent2);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
            }
        }
    private:
        juce::Point<float> toXY (float inDb, float outDb, juce::Rectangle<float> b) const
        {
            return { juce::jmap (juce::jlimit (-60.0f, 0.0f, inDb),  -60.0f, 0.0f, b.getX(), b.getRight()),
                     juce::jmap (juce::jlimit (-60.0f, 0.0f, outDb), -60.0f, 0.0f, b.getBottom(), b.getY()) };
        }
        float thr_ = -18.0f, ratio_ = 4.0f, knee_ = 6.0f, opIn_ = -100.0f, opGr_ = 0.0f;
    };

    /** Scrolling gain-reduction history. */
    class GrHistoryComponent : public juce::Component
    {
    public:
        GrHistoryComponent() { hist_.fill (0.0f); }
        void push (float grDb) { hist_[(size_t) head_] = -grDb; head_ = (head_ + 1) % (int) hist_.size(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (colours::panel); g.fillRoundedRectangle (b, 3.0f);

            const int N = (int) hist_.size();
            juce::Path p; p.startNewSubPath (b.getX(), b.getBottom());
            for (int i = 0; i < N; ++i)
            {
                const float mag = hist_[(size_t) ((head_ + i) % N)];
                const float x = juce::jmap ((float) i, 0.0f, (float) (N - 1), b.getX(), b.getRight());
                const float y = juce::jmap (juce::jlimit (0.0f, 30.0f, mag), 0.0f, 30.0f, b.getY(), b.getBottom());
                p.lineTo (x, y);
            }
            p.lineTo (b.getRight(), b.getBottom()); p.closeSubPath();
            g.setColour (colours::accent2.withAlpha (0.35f));
            g.fillPath (p);
        }
    private:
        std::array<float, 256> hist_ {};
        int head_ = 0;
    };
}
