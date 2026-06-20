#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"
#include "../ParamIDs.h"

#include <array>
#include <cmath>

namespace gf::ui
{
    /**
        The Multiband tab: a log-frequency EQ-style display with two draggable
        crossover dividers, three shaded band regions, and a live per-band
        gain-reduction curve (each band visibly dips by how hard it's compressing).
        Below it, one control column per band (threshold / ratio / trim / bypass),
        plus the global enable + solo.
    */
    class MultibandView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MultibandView (GlueForgeProcessor& p) : proc (p)
        {
            using namespace gf::params;

            setTooltip ("Drag the crossover lines to set where the Low/Mid/High bands split. "
                        "Each band's shelf dips by how hard that band is compressing right now.");

            enableBtn.setButtonText ("Multiband ON");
            enableBtn.setTooltip ("Switch from single-band to 3-band multiband processing.");
            addAndMakeVisible (enableBtn);
            enableAtt = std::make_unique<ButtonAtt> (proc.apvts, id::mbEnable, enableBtn);

            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id::mbSolo)))
            { int i = 1; for (auto& c : cp->choices) soloBox.addItem (c, i++); }
            soloBox.setTooltip ("Solo one band to hear it on its own while you dial it in.");
            addAndMakeVisible (soloBox);
            soloAtt = std::make_unique<ComboAtt> (proc.apvts, id::mbSolo, soloBox);
            soloLabel.setText ("Solo", juce::dontSendNotification);
            soloLabel.setFont (juce::FontOptions (11.0f));
            soloLabel.setColour (juce::Label::textColourId, colours::dim);
            addAndMakeVisible (soloLabel);

            const char* thrId[3]  { id::mbThresh1, id::mbThresh2, id::mbThresh3 };
            const char* ratId[3]  { id::mbRatio1,  id::mbRatio2,  id::mbRatio3 };
            const char* trimId[3] { id::mbTrim1,   id::mbTrim2,   id::mbTrim3 };
            const char* bypId[3]  { id::mbBypass1, id::mbBypass2, id::mbBypass3 };
            const char* bandName[3] { "Low", "Mid", "High" };

            for (int b = 0; b < 3; ++b)
            {
                const juce::String bn (bandName[b]);
                setupRotary (thr[(size_t) b]);   thrA[(size_t) b]  = std::make_unique<SliderAtt> (proc.apvts, thrId[b],  thr[(size_t) b]);
                setupRotary (ratio[(size_t) b]); ratioA[(size_t) b]= std::make_unique<SliderAtt> (proc.apvts, ratId[b],  ratio[(size_t) b]);
                setupRotary (trim[(size_t) b]);  trimA[(size_t) b] = std::make_unique<SliderAtt> (proc.apvts, trimId[b], trim[(size_t) b]);
                thr[(size_t) b]  .setTooltip (bn + " band threshold (dB): level above which this band compresses.");
                ratio[(size_t) b].setTooltip (bn + " band ratio: how hard this band is compressed above its threshold.");
                trim[(size_t) b] .setTooltip (bn + " band output trim (dB), after compression.");
                byp[(size_t) b].setButtonText ("Bypass");
                byp[(size_t) b].setTooltip ("Bypass compression on the " + bn + " band (it stays in the mix).");
                addAndMakeVisible (byp[(size_t) b]);
                bypA[(size_t) b] = std::make_unique<ButtonAtt> (proc.apvts, bypId[b], byp[(size_t) b]);
            }
        }

        void tick() { repaint(); } // live per-band GR

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);

            auto top = r.removeFromTop (24);
            enableBtn.setBounds (top.removeFromLeft (130));
            top.removeFromLeft (16);
            soloLabel.setBounds (top.removeFromLeft (32));
            soloBox.setBounds (top.removeFromLeft (130));

            r.removeFromTop (6);
            eqArea = r.removeFromTop (juce::jmax (140, r.getHeight() * 2 / 5));
            r.removeFromTop (8);

            // Three band columns of controls.
            const int colW = r.getWidth() / 3;
            const char* names[3] { "LOW", "MID", "HIGH" };
            juce::ignoreUnused (names);
            for (int b = 0; b < 3; ++b)
            {
                auto col = r.removeFromLeft (colW).reduced (6, 0);
                col.removeFromTop (16); // band header (drawn in paint)
                auto knobs = col.removeFromTop (juce::jmin (78, col.getHeight() - 28));
                const int kw = knobs.getWidth() / 3;
                thr[(size_t) b].setBounds   (knobs.removeFromLeft (kw).reduced (2));
                ratio[(size_t) b].setBounds (knobs.removeFromLeft (kw).reduced (2));
                trim[(size_t) b].setBounds  (knobs.removeFromLeft (kw).reduced (2));
                byp[(size_t) b].setBounds   (col.removeFromTop (22).withSizeKeepingCentre (col.getWidth(), 22));
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (colours::bg);
            paintEq (g, eqArea.toFloat());

            // Band column headers + tiny knob captions.
            const char* names[3] { "LOW", "MID", "HIGH" };
            const juce::Colour bandCol[3] { colours::accent, colours::accent2, juce::Colour (0xff7aa2f7) };
            auto r = getLocalBounds().reduced (8);
            r.removeFromTop (24 + 6); r.removeFromTop (eqArea.getHeight()); r.removeFromTop (8);
            const int colW = r.getWidth() / 3;
            for (int b = 0; b < 3; ++b)
            {
                auto col = r.removeFromLeft (colW).reduced (6, 0);
                auto hdr = col.removeFromTop (16);
                g.setColour (bandCol[b]); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (names[b], hdr, juce::Justification::centredLeft, false);
                g.setColour (colours::dim); g.setFont (juce::FontOptions (9.0f));
                auto caps = col.removeFromTop (12);
                const int kw = caps.getWidth() / 3;
                g.drawText ("Thr",   caps.removeFromLeft (kw), juce::Justification::centred, false);
                g.drawText ("Ratio", caps.removeFromLeft (kw), juce::Justification::centred, false);
                g.drawText ("Trim",  caps.removeFromLeft (kw), juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override { dragDivider = dividerAt (e.position.x); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragDivider < 0) return;
            const float f = xToFreq (e.position.x);
            const char* pid = dragDivider == 0 ? gf::params::id::mbXLow : gf::params::id::mbXHigh;
            if (auto* prm = proc.apvts.getParameter (pid))
                prm->setValueNotifyingHost (prm->convertTo0to1 (f));
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { dragDivider = -1; }

    private:
        using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

        void setupRotary (juce::Slider& s)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 13);
            addAndMakeVisible (s);
        }

        float raw (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

        float freqToX (float f, juce::Rectangle<float> b) const
        {
            return juce::jmap (std::log10 (juce::jlimit (20.0f, 20000.0f, f)),
                               std::log10 (20.0f), std::log10 (20000.0f), b.getX(), b.getRight());
        }
        float xToFreq (float x) const
        {
            auto b = eqArea.toFloat();
            const float l = juce::jmap (x, b.getX(), b.getRight(), std::log10 (20.0f), std::log10 (20000.0f));
            return std::pow (10.0f, l);
        }
        int dividerAt (float x) const
        {
            auto b = eqArea.toFloat();
            if (std::abs (x - freqToX (raw (gf::params::id::mbXLow),  b)) <= 6.0f) return 0;
            if (std::abs (x - freqToX (raw (gf::params::id::mbXHigh), b)) <= 6.0f) return 1;
            return -1;
        }

        void paintEq (juce::Graphics& g, juce::Rectangle<float> b)
        {
            g.setColour (colours::panel); g.fillRoundedRectangle (b, 3.0f);

            // Frequency grid + labels.
            g.setFont (juce::FontOptions (9.0f));
            for (int f : { 50, 100, 200, 500, 1000, 2000, 5000, 10000 })
            {
                const float x = freqToX ((float) f, b);
                g.setColour (colours::track.withAlpha (0.5f));
                g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
                g.setColour (colours::dim);
                g.drawText (f >= 1000 ? juce::String (f / 1000) + "k" : juce::String (f),
                            (int) x + 2, (int) b.getBottom() - 12, 28, 12, juce::Justification::left, false);
            }

            const float xLow  = freqToX (raw (gf::params::id::mbXLow),  b);
            const float xHigh = freqToX (raw (gf::params::id::mbXHigh), b);
            const float edges[4] { b.getX(), xLow, xHigh, b.getRight() };
            const juce::Colour bandCol[3] { colours::accent, colours::accent2, juce::Colour (0xff7aa2f7) };
            const char* bypId[3] { gf::params::id::mbBypass1, gf::params::id::mbBypass2, gf::params::id::mbBypass3 };
            const int   solo = (int) raw (gf::params::id::mbSolo) - 1; // -1 none

            for (int i = 0; i < 3; ++i)
            {
                juce::Rectangle<float> region (edges[i], b.getY(), juce::jmax (1.0f, edges[i + 1] - edges[i]), b.getHeight());
                const bool active = (raw (bypId[i]) < 0.5f) && (solo < 0 || solo == i);
                g.setColour (bandCol[i].withAlpha (active ? 0.10f : 0.03f));
                g.fillRect (region);

                // Live gain-reduction "shelf": the band dips from the top by its GR.
                const float grMag = juce::jlimit (0.0f, 36.0f, -proc.getBandGainReductionDb (i));
                const float y = juce::jmap (grMag, 0.0f, 36.0f, b.getY() + 2.0f, b.getBottom() - 2.0f);
                g.setColour (bandCol[i].withAlpha (active ? 0.85f : 0.25f));
                g.fillRect (region.getX(), b.getY() + 1.0f, region.getWidth(), y - b.getY()); // fill from top down to the shelf
                g.fillRect (region.getX(), y, region.getWidth(), 2.0f);                       // the shelf line
            }

            // Crossover dividers.
            g.setColour (colours::text.withAlpha (0.8f));
            g.drawVerticalLine ((int) xLow,  b.getY(), b.getBottom());
            g.drawVerticalLine ((int) xHigh, b.getY(), b.getBottom());
            g.setColour (colours::dim); g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (juce::roundToInt (raw (gf::params::id::mbXLow)))  + " Hz", (int) xLow + 2,  (int) b.getY() + 2, 50, 12, juce::Justification::left, false);
            g.drawText (juce::String (juce::roundToInt (raw (gf::params::id::mbXHigh))) + " Hz", (int) xHigh + 2, (int) b.getY() + 2, 50, 12, juce::Justification::left, false);
        }

        GlueForgeProcessor& proc;
        juce::ToggleButton enableBtn;
        std::unique_ptr<ButtonAtt> enableAtt;
        juce::ComboBox soloBox; juce::Label soloLabel;
        std::unique_ptr<ComboAtt> soloAtt;
        std::array<juce::Slider, 3> thr, ratio, trim;
        std::array<juce::ToggleButton, 3> byp;
        std::array<std::unique_ptr<SliderAtt>, 3> thrA, ratioA, trimA;
        std::array<std::unique_ptr<ButtonAtt>, 3> bypA;
        juce::Rectangle<int> eqArea;
        int dragDivider = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandView)
    };
}
