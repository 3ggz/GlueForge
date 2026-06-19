#pragma once

#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Components.h"
#include "ui/Presets.h"
#include "ui/ShapeEditor.h"

#include <map>
#include <memory>

/**
    Full GlueForge editor: dark producer UI with grouped controls, GR + I/O
    meters, a live transfer-curve display, a gain-reduction history graph, a
    factory preset menu, A/B compare, and file-based user presets. A 30 Hz timer
    polls the processor's metering snapshots.
*/
class GlueForgeEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit GlueForgeEditor (GlueForgeProcessor&);
    ~GlueForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Rotary { juce::Slider s; juce::Label l; std::unique_ptr<SliderAtt> a; };
    struct Combo  { juce::ComboBox c; juce::Label l; std::unique_ptr<ComboAtt> a; };
    struct Toggle { juce::ToggleButton b; std::unique_ptr<ButtonAtt> a; };

    void timerCallback() override;

    void addRotary (const juce::String& id, const juce::String& name);
    void addCombo  (const juce::String& id, const juce::String& name);
    void addToggle (const juce::String& id, const juce::String& name);
    void layoutRow (juce::Rectangle<int> area, const juce::StringArray& ids);

    void rebuildPresetMenu();
    void savePresetToFile();
    void loadPresetFromFile();

    GlueForgeProcessor& proc;
    gf::ui::LookAndFeel lnf;

    // Displays
    gf::ui::LevelMeter inMeter, outMeter;
    gf::ui::GrMeter    grMeter;
    juce::Label inLbl { {}, "IN" }, outLbl { {}, "OUT" }, grLbl { {}, "GR" };
    gf::ui::TransferCurveComponent curve;
    gf::ui::GrHistoryComponent     history;
    gf::ui::ShapeEditor            shapeEditor;            // tempo-duck pump shape
    juce::Label displayCaption { {}, "TRANSFER CURVE" };  // names the centre display
    int lastTrigger = -1;                                 // for mode-aware display switching

    // Top bar
    juce::ComboBox  presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, loadBtn { "Load" };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "A>B" };
    juce::ToggleButton bypassBtn { "Bypass" };
    std::unique_ptr<ButtonAtt> bypassAtt;

    std::vector<gf::ui::Preset> presets;
    juce::ValueTree stateA, stateB;
    bool showingA = true;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> rowHeaders; // section titles (drawn in paint)

    std::map<juce::String, std::unique_ptr<Rotary>> rotaries;
    std::map<juce::String, std::unique_ptr<Combo>>  combos;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueForgeEditor)
};
