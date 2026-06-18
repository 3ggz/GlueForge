#include "PluginEditor.h"
#include "ParamIDs.h"

GlueForgeEditor::GlueForgeEditor (GlueForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 22);
    addAndMakeVisible (gainSlider);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (gainLabel);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, gf::params::id::gain, gainSlider);

    setSize (360, 320);
    setResizable (true, true);
    setResizeLimits (280, 260, 900, 760);
}

void GlueForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d23));

    g.setColour (juce::Colour (0xfff0f0f0));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("GlueForge", getLocalBounds().removeFromTop (52),
                juce::Justification::centred);

    g.setColour (juce::Colour (0xff6b7280));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v0.1 · Phase 1 skeleton",
                getLocalBounds().removeFromBottom (24),
                juce::Justification::centred);
}

void GlueForgeEditor::resized()
{
    auto r = getLocalBounds().reduced (24);
    r.removeFromTop (52);
    r.removeFromBottom (24);
    gainLabel.setBounds (r.removeFromTop (24));
    gainSlider.setBounds (r);
}
