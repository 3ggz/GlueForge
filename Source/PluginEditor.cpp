#include "PluginEditor.h"
#include "ParamIDs.h"

using namespace gf;

GlueForgeEditor::GlueForgeEditor (GlueForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // --- Displays ---
    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (curve);
    addAndMakeVisible (history);
    for (auto* l : { &inLbl, &grLbl, &outLbl })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setFont (juce::FontOptions (10.0f));
        l->setColour (juce::Label::textColourId, ui::colours::dim);
        addAndMakeVisible (*l);
    }

    // --- Top bar ---
    presets = ui::factoryPresets();
    rebuildPresetMenu();
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (idx, (int) presets.size()))
            ui::applyPreset (proc.apvts, presets[(size_t) idx]);
    };
    addAndMakeVisible (presetBox);

    prevBtn.onClick = [this] { presetBox.setSelectedId (juce::jmax (1, presetBox.getSelectedId() - 1)); };
    nextBtn.onClick = [this] { presetBox.setSelectedId (juce::jmin ((int) presets.size(), presetBox.getSelectedId() + 1)); };
    saveBtn.onClick = [this] { savePresetToFile(); };
    loadBtn.onClick = [this] { loadPresetFromFile(); };
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &loadBtn }) addAndMakeVisible (*b);

    // A/B compare
    stateA = proc.apvts.copyState();
    stateB = proc.apvts.copyState();
    aBtn.setClickingTogglesState (true); bBtn.setClickingTogglesState (true);
    aBtn.setToggleState (true, juce::dontSendNotification);
    aBtn.setRadioGroupId (100); bBtn.setRadioGroupId (100);
    auto storeCurrentInto = [this] (juce::ValueTree& dest) { dest = proc.apvts.copyState(); };
    auto recall = [this] (const juce::ValueTree& src)
    {
        if (src.isValid()) proc.apvts.replaceState (src.createCopy());
    };
    aBtn.onClick = [this, storeCurrentInto, recall]
    {
        if (! showingA) { storeCurrentInto (stateB); showingA = true; recall (stateA); }
    };
    bBtn.onClick = [this, storeCurrentInto, recall]
    {
        if (showingA) { storeCurrentInto (stateA); showingA = false; recall (stateB); }
    };
    copyBtn.onClick = [this] { (showingA ? stateB : stateA) = proc.apvts.copyState(); };
    for (auto* b : { &aBtn, &bBtn, &copyBtn }) addAndMakeVisible (*b);

    bypassBtn.setButtonText ("Bypass");
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, params::id::bypass, bypassBtn);
    addAndMakeVisible (bypassBtn);

    // --- Controls ---
    addRotary (params::id::threshold, "Thresh");
    addRotary (params::id::ratio,     "Ratio");
    addRotary (params::id::knee,      "Knee");
    addRotary (params::id::attack,    "Attack");
    addRotary (params::id::release,   "Release");
    addRotary (params::id::hold,      "Hold");

    addRotary (params::id::detector,  "Detect");
    addRotary (params::id::range,     "Max GR");
    addRotary (params::id::link,      "Link");
    addRotary (params::id::makeup,    "Makeup");
    addRotary (params::id::gain,      "Output");
    addToggle (params::id::automakeup, "Auto");

    addCombo  (params::id::trigger,   "Trigger");
    addRotary (params::id::scHpf,     "SC HPF");
    addRotary (params::id::scLpf,     "SC LPF");
    addToggle (params::id::scListen,  "Listen");

    addCombo  (params::id::duckRate,  "Duck Rate");
    addRotary (params::id::duckDepth, "Depth");
    addCombo  (params::id::character, "Character");
    addRotary (params::id::drive,     "Drive");
    addRotary (params::id::satMix,    "Sat Mix");
    addRotary (params::id::mix,       "Mix");
    addRotary (params::id::lookahead, "Look");
    addCombo  (params::id::oversampling, "OS");

    addToggle (params::id::midside,  "M/S");
    addToggle (params::id::mbEnable, "MultiB");
    addRotary (params::id::mbXLow,   "X Low");
    addRotary (params::id::mbXHigh,  "X High");
    addCombo  (params::id::mbSolo,   "Solo");
    addRotary (params::id::mbTrim1,  "Low");
    addToggle (params::id::mbBypass1, "Lo Byp");
    addRotary (params::id::mbTrim2,  "Mid");
    addToggle (params::id::mbBypass2, "Md Byp");
    addRotary (params::id::mbTrim3,  "High");
    addToggle (params::id::mbBypass3, "Hi Byp");

    setResizable (true, true);
    setResizeLimits (860, 680, 1500, 1200);
    setSize (940, 840);

    startTimerHz (30);
}

GlueForgeEditor::~GlueForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlueForgeEditor::addRotary (const juce::String& id, const juce::String& name)
{
    auto r = std::make_unique<Rotary>();
    r->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    r->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    addAndMakeVisible (r->s);
    r->l.setText (name, juce::dontSendNotification);
    r->l.setJustificationType (juce::Justification::centred);
    r->l.setFont (juce::FontOptions (11.0f));
    r->l.setColour (juce::Label::textColourId, ui::colours::dim);
    addAndMakeVisible (r->l);
    r->a = std::make_unique<SliderAtt> (proc.apvts, id, r->s);
    rotaries[id] = std::move (r);
}

void GlueForgeEditor::addCombo (const juce::String& id, const juce::String& name)
{
    auto c = std::make_unique<Combo>();
    if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        int i = 1;
        for (auto& choice : cp->choices) c->c.addItem (choice, i++);
    }
    addAndMakeVisible (c->c);
    c->l.setText (name, juce::dontSendNotification);
    c->l.setJustificationType (juce::Justification::centred);
    c->l.setFont (juce::FontOptions (11.0f));
    c->l.setColour (juce::Label::textColourId, ui::colours::dim);
    addAndMakeVisible (c->l);
    c->a = std::make_unique<ComboAtt> (proc.apvts, id, c->c);
    combos[id] = std::move (c);
}

void GlueForgeEditor::addToggle (const juce::String& id, const juce::String& name)
{
    auto t = std::make_unique<Toggle>();
    t->b.setButtonText (name);
    addAndMakeVisible (t->b);
    t->a = std::make_unique<ButtonAtt> (proc.apvts, id, t->b);
    toggles[id] = std::move (t);
}

void GlueForgeEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    juce::String lastCat;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].category != lastCat)
        {
            lastCat = presets[(size_t) i].category;
            presetBox.addSectionHeading (lastCat);
        }
        presetBox.addItem (presets[(size_t) i].name, i + 1);
    }
    presetBox.setSelectedId (1, juce::dontSendNotification);
}

void GlueForgeEditor::timerCallback()
{
    inMeter.setLevelDb  (proc.getInputLevelDb());   inMeter.repaint();
    outMeter.setLevelDb (proc.getOutputLevelDb());  outMeter.repaint();
    const float gr = proc.getCurrentGainReductionDb();
    grMeter.setReductionDb (gr);                    grMeter.repaint();

    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    curve.setParams (raw (params::id::threshold), raw (params::id::ratio), raw (params::id::knee));
    curve.setOperatingPoint (proc.getInputLevelDb(), gr);
    curve.repaint();

    history.push (gr); history.repaint();
}

void GlueForgeEditor::layoutRow (juce::Rectangle<int> area, const juce::StringArray& ids)
{
    const int n = ids.size();
    if (n == 0) return;
    const int cellW = area.getWidth() / n;
    for (auto& id : ids)
    {
        auto cell = area.removeFromLeft (cellW).reduced (4, 2);
        if (auto it = rotaries.find (id); it != rotaries.end())
        {
            it->second->l.setBounds (cell.removeFromTop (15));
            it->second->s.setBounds (cell);
        }
        else if (auto ci = combos.find (id); ci != combos.end())
        {
            ci->second->l.setBounds (cell.removeFromTop (15));
            ci->second->c.setBounds (cell.removeFromTop (26));
        }
        else if (auto ti = toggles.find (id); ti != toggles.end())
        {
            ti->second->b.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 24));
        }
    }
}

void GlueForgeEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    // Top bar
    auto top = r.removeFromTop (32);
    bypassBtn.setBounds (top.removeFromRight (84));
    top.removeFromRight (6);
    copyBtn.setBounds (top.removeFromRight (40));
    bBtn.setBounds (top.removeFromRight (30));
    aBtn.setBounds (top.removeFromRight (30));
    top.removeFromRight (8);
    loadBtn.setBounds (top.removeFromRight (50));
    saveBtn.setBounds (top.removeFromRight (50));
    top.removeFromRight (8);
    presetBox.setBounds (top.removeFromLeft (juce::jmin (260, top.getWidth() - 70)));
    top.removeFromLeft (4);
    prevBtn.setBounds (top.removeFromLeft (28));
    top.removeFromLeft (2);
    nextBtn.setBounds (top.removeFromLeft (28));

    r.removeFromTop (10);

    // Displays: meters | transfer curve | GR history
    auto disp = r.removeFromTop (168);
    auto meters = disp.removeFromLeft (104);
    const int mw = (meters.getWidth() - 8) / 3;
    auto placeMeter = [&] (juce::Component& m, juce::Label& lbl)
    {
        auto col = meters.removeFromLeft (mw);
        lbl.setBounds (col.removeFromBottom (14));
        m.setBounds (col.reduced (2));
        meters.removeFromLeft (4);
    };
    placeMeter (inMeter, inLbl);
    placeMeter (grMeter, grLbl);
    placeMeter (outMeter, outLbl);

    disp.removeFromLeft (8);
    curve.setBounds (disp.removeFromLeft (disp.getWidth() / 2 - 4));
    disp.removeFromLeft (8);
    history.setBounds (disp);

    r.removeFromTop (10);

    // Control sections (4 equal rows)
    const juce::StringArray row1 { params::id::threshold, params::id::ratio, params::id::knee,
                                   params::id::attack, params::id::release, params::id::hold };
    const juce::StringArray row2 { params::id::detector, params::id::range, params::id::link,
                                   params::id::makeup, params::id::gain, params::id::automakeup };
    const juce::StringArray row3 { params::id::trigger, params::id::scHpf, params::id::scLpf, params::id::scListen };
    const juce::StringArray row4 { params::id::duckRate, params::id::duckDepth,
                                   params::id::character, params::id::drive, params::id::satMix };
    const juce::StringArray row5 { params::id::mix, params::id::lookahead, params::id::oversampling };
    const juce::StringArray row6 { params::id::midside, params::id::mbEnable, params::id::mbXLow,
                                   params::id::mbXHigh, params::id::mbSolo };
    const juce::StringArray row7 { params::id::mbTrim1, params::id::mbBypass1, params::id::mbTrim2,
                                   params::id::mbBypass2, params::id::mbTrim3, params::id::mbBypass3 };

    const int rh = r.getHeight() / 7;
    layoutRow (r.removeFromTop (rh), row1);
    layoutRow (r.removeFromTop (rh), row2);
    layoutRow (r.removeFromTop (rh), row3);
    layoutRow (r.removeFromTop (rh), row4);
    layoutRow (r.removeFromTop (rh), row5);
    layoutRow (r.removeFromTop (rh), row6);
    layoutRow (r, row7);
}

void GlueForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::bg);
    g.setColour (ui::colours::accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("GLUEFORGE", getLocalBounds().removeFromTop (32).withTrimmedLeft (0).removeFromRight (130),
                juce::Justification::centredRight, false);
}

void GlueForgeEditor::savePresetToFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("GlueForge").getChildFile ("Presets");
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save GlueForge preset", dir, "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            if (auto xml = proc.apvts.copyState().createXml())
                xml->writeTo (f.withFileExtension ("xml"));
        });
}

void GlueForgeEditor::loadPresetFromFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("GlueForge").getChildFile ("Presets");
    chooser = std::make_unique<juce::FileChooser> ("Load GlueForge preset", dir, "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{} || ! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
                if (xml->hasTagName (proc.apvts.state.getType()))
                    proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        });
}
