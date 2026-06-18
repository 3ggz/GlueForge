#pragma once

#include "PluginProcessor.h"

/**
    Phase 1 editor — deliberately minimal: a single rotary Gain control on a
    dark background. The full metering/transfer-curve UI arrives in Phase 6.
*/
class GlueForgeEditor : public juce::AudioProcessorEditor
{
public:
    explicit GlueForgeEditor (GlueForgeProcessor&);
    ~GlueForgeEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GlueForgeProcessor& proc;

    juce::Slider gainSlider;
    juce::Label  gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueForgeEditor)
};
