#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gf::ui
{
    namespace colours
    {
        const juce::Colour bg      { 0xff15171c };
        const juce::Colour panel   { 0xff20232b };
        const juce::Colour panel2  { 0xff2a2e38 };
        const juce::Colour text    { 0xffe9ecf1 };
        const juce::Colour dim     { 0xff8b919c };
        const juce::Colour accent  { 0xff5ad1c4 }; // teal
        const juce::Colour accent2 { 0xfff0a35e }; // amber (GR)
        const juce::Colour track   { 0xff3a3f4b };
    }

    /** Dark, producer-friendly look: flat panels, arc knobs, teal accent. */
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
            setColour (juce::Slider::textBoxTextColourId,        colours::text);
            setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
            setColour (juce::Label::textColourId,                colours::text);
            setColour (juce::ComboBox::backgroundColourId,       colours::panel2);
            setColour (juce::ComboBox::textColourId,             colours::text);
            setColour (juce::ComboBox::outlineColourId,          colours::track);
            setColour (juce::PopupMenu::backgroundColourId,      colours::panel2);
            setColour (juce::TextButton::buttonColourId,         colours::panel2);
            setColour (juce::TextButton::textColourOffId,        colours::text);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle,
                               juce::Slider&) override
        {
            const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
            const auto r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            const auto cx = b.getCentreX(), cy = b.getCentreY();
            const float angle = startAngle + pos * (endAngle - startAngle);
            const float thick = juce::jmax (2.5f, r * 0.16f);

            juce::Path track;
            track.addCentredArc (cx, cy, r, r, 0.0f, startAngle, endAngle, true);
            g.setColour (colours::track);
            g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path val;
            val.addCentredArc (cx, cy, r, r, 0.0f, startAngle, angle, true);
            g.setColour (colours::accent);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float pr = r - thick;
            g.setColour (colours::text);
            juce::Point<float> tip (cx + pr * std::cos (angle - juce::MathConstants<float>::halfPi),
                                    cy + pr * std::sin (angle - juce::MathConstants<float>::halfPi));
            g.drawLine ({ { cx, cy }, tip }, juce::jmax (1.5f, thick * 0.5f));
            g.setColour (colours::panel2);
            g.fillEllipse (juce::Rectangle<float> (r * 0.5f, r * 0.5f).withCentre ({ cx, cy }));
        }

        juce::Font getLabelFont (juce::Label&) override { return juce::FontOptions (12.0f); }
    };
}
