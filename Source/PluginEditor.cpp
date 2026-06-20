#include "PluginEditor.h"
#include "ParamIDs.h"
#include "BinaryData.h"

using namespace gf;

GlueForgeEditor::GlueForgeEditor (GlueForgeProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), shapeEditor (p), mbView (p)
{
    setLookAndFeel (&lnf);

    // --- Displays ---
    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (curve);
    addAndMakeVisible (history);
    addAndMakeVisible (shapeEditor);
    shapeEditor.setVisible (false); // shown only in Tempo Duck mode

    inMeter.setTooltip  ("Input level (peak), 36 dB scale.");
    outMeter.setTooltip ("Output level (peak) after all processing.");
    grMeter.setTooltip  ("Gain reduction right now, in dB. Aim for musical amounts (often 2-6 dB for glue).");
    curve.setTooltip    ("Transfer curve: your in-to-out mapping. The moving dot is the live operating point.");
    history.setTooltip  ("Gain-reduction history: a scrolling view of how the compressor breathes over time.");
    displayCaption.setJustificationType (juce::Justification::centredLeft);
    displayCaption.setFont (juce::FontOptions (10.0f));
    displayCaption.setColour (juce::Label::textColourId, ui::colours::dim);
    addAndMakeVisible (displayCaption);
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
        {
            ui::applyPreset (proc.apvts, presets[(size_t) idx]);
            proc.rebuildShapeLut(); // preset carries the pump shape
        }
    };
    presetBox.setTooltip ("Factory presets, grouped by category.");
    addAndMakeVisible (presetBox);

    prevBtn.onClick = [this] { presetBox.setSelectedId (juce::jmax (1, presetBox.getSelectedId() - 1)); };
    nextBtn.onClick = [this] { presetBox.setSelectedId (juce::jmin ((int) presets.size(), presetBox.getSelectedId() + 1)); };
    saveBtn.onClick = [this] { savePresetToFile(); };
    loadBtn.onClick = [this] { loadPresetFromFile(); };
    prevBtn.setTooltip ("Previous preset.");
    nextBtn.setTooltip ("Next preset.");
    saveBtn.setTooltip ("Save the current settings as a preset (.xml).");
    loadBtn.setTooltip ("Load a preset (.xml) from disk.");
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
        if (src.isValid()) { proc.apvts.replaceState (src.createCopy()); proc.rebuildShapeLut(); }
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
    aBtn.setTooltip ("Recall snapshot A.");
    bBtn.setTooltip ("Recall snapshot B.");
    copyBtn.setTooltip ("Copy the current settings into the other A/B slot.");
    for (auto* b : { &aBtn, &bBtn, &copyBtn }) addAndMakeVisible (*b);

    bypassBtn.setButtonText ("Bypass");
    bypassBtn.setTooltip ("Bypass the whole plug-in.");
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, params::id::bypass, bypassBtn);
    addAndMakeVisible (bypassBtn);

    // --- Controls (each carries a descriptive tooltip for the ? hover help) ---
    addRotary (params::id::threshold, "Thresh",  "Level (dB) above which compression starts. Lower it for more gain reduction.");
    addRotary (params::id::ratio,     "Ratio",   "How hard signal above the threshold is reduced. 2:1 gentle, 4:1 control, 10:1+ limiting.");
    addRotary (params::id::knee,      "Knee",    "Width (dB) of the soft transition around the threshold. Higher = smoother onset.");
    addRotary (params::id::attack,    "Attack",  "How fast (ms) it clamps once over threshold. Fast catches transients; slow lets punch through.");
    addRotary (params::id::release,   "Release", "How fast (ms) it recovers after the signal drops. Short = pumpy, long = smooth.");
    addRotary (params::id::hold,      "Hold",    "Holds full gain reduction (ms) before release begins. Steadies choppy material.");

    addRotary (params::id::detector,  "Detect",  "Blends Peak (transient-accurate) toward RMS (loudness-like, smoother) detection.");
    addRotary (params::id::range,     "Max GR",  "Caps the maximum gain reduction. Fully up = Off (unlimited).");
    addRotary (params::id::link,      "Link",    "Stereo link: 100% = shared detector (stable image), 0% = independent per channel.");
    addRotary (params::id::makeup,    "Makeup",  "Manual make-up gain (dB) to compensate for the level lost to compression.");
    addRotary (params::id::gain,      "Output",  "Final output level trim (dB), after everything.");
    addToggle (params::id::automakeup, "Auto",   "Auto make-up: applies estimated gain automatically as you change threshold/ratio.");

    addCombo  (params::id::trigger,   "Trigger", "What drives compression: Internal, External Sidechain, or tempo-synced Duck.");
    addRotary (params::id::scHpf,     "SC HPF",  "High-pass (Hz) on the detection signal. Raise so deep bass doesn't trigger ducking.");
    addRotary (params::id::scLpf,     "SC LPF",  "Low-pass (Hz) on the detection signal. Lower to ignore highs/sibilance.");
    addToggle (params::id::scListen,  "Listen",  "Audition the (filtered) detection signal the compressor reacts to.");

    addCombo  (params::id::duckRate,  "Duck Rate", "Tempo Duck cycle length, synced to the host tempo (e.g. 1/4, 1/8).");
    addRotary (params::id::duckDepth, "Depth",   "How deep each tempo-duck pump dips (dB).");
    addCombo  (params::id::character, "Character", "Compressor flavour: VCA (clean/punchy), FET (aggressive), Opto (smooth).");
    addRotary (params::id::drive,     "Drive",   "How hard the signal is pushed into the character/saturation stage.");
    addRotary (params::id::satMix,    "Sat Mix", "Blend of the saturated signal. 0% = clean; raise for warmth/harmonics.");
    addRotary (params::id::mix,       "Mix",     "Dry/wet blend for parallel compression. 100% = fully compressed.");
    addRotary (params::id::lookahead, "Look",    "Lookahead (ms) so transients are caught early. Adds reported latency (PDC).");
    addCombo  (params::id::oversampling, "OS",   "Oversampling reduces aliasing from fast comp/saturation. Higher = cleaner, more CPU.");

    addToggle (params::id::midside, "M/S",       "Process Mid/Side instead of Left/Right.");

    // Tabs (compressor / multiband) — the MB controls live in mbView.
    addAndMakeVisible (mbView);
    mbView.setVisible (false);
    for (auto* t : { &compTabBtn, &mbTabBtn })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (200);
        addAndMakeVisible (*t);
    }
    compTabBtn.setToggleState (true, juce::dontSendNotification);
    compTabBtn.onClick = [this] { showTab (0); };
    mbTabBtn.onClick   = [this] { showTab (1); };
    compTabBtn.setTooltip ("Compressor page: the main single-band engine.");
    mbTabBtn.setTooltip   ("Multiband page: three independent bands with an EQ-style display.");

    // Hover-help toggle (?) + Manual button — always available on the tabs row.
    helpBtn.setClickingTogglesState (true);
    helpBtn.setToggleState (true, juce::dontSendNotification);
    helpBtn.setTooltip ("Toggle hover help (tooltips) on every control.");
    helpBtn.onClick = [this] { tooltips.setHelpEnabled (helpBtn.getToggleState()); };
    addAndMakeVisible (helpBtn);

    manualBtn.setTooltip ("Open the GlueForge user manual (PDF).");
    manualBtn.onClick = [this] { openManual(); };
    addAndMakeVisible (manualBtn);

    setResizable (true, true);
    setResizeLimits (900, 660, 1500, 1300);
    setSize (980, 780);

    startTimerHz (30);
    showTab (0);
}

void GlueForgeEditor::showTab (int t)
{
    currentTab = t;
    const bool comp = (t == 0);

    for (auto& kv : rotaries) { kv.second->s.setVisible (comp); kv.second->l.setVisible (comp); }
    for (auto& kv : combos)   { kv.second->c.setVisible (comp); kv.second->l.setVisible (comp); }
    for (auto& kv : toggles)  { kv.second->b.setVisible (comp); }
    inMeter.setVisible (comp); grMeter.setVisible (comp); outMeter.setVisible (comp);
    inLbl.setVisible (comp); grLbl.setVisible (comp); outLbl.setVisible (comp);
    history.setVisible (comp);
    displayCaption.setVisible (comp);
    if (! comp) { curve.setVisible (false); shapeEditor.setVisible (false); }
    else          lastTrigger = -1; // force the mode-aware display to re-show next tick

    mbView.setVisible (! comp);
    compTabBtn.setToggleState (comp, juce::dontSendNotification);
    mbTabBtn.setToggleState (! comp, juce::dontSendNotification);
    repaint();
}

GlueForgeEditor::~GlueForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void GlueForgeEditor::addRotary (const juce::String& id, const juce::String& name, const juce::String& tip)
{
    auto r = std::make_unique<Rotary>();
    r->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    r->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    r->s.setTooltip (tip);
    addAndMakeVisible (r->s);
    r->l.setText (name, juce::dontSendNotification);
    r->l.setJustificationType (juce::Justification::centred);
    r->l.setFont (juce::FontOptions (11.0f));
    r->l.setColour (juce::Label::textColourId, ui::colours::dim);
    addAndMakeVisible (r->l);
    r->a = std::make_unique<SliderAtt> (proc.apvts, id, r->s);
    rotaries[id] = std::move (r);
}

void GlueForgeEditor::addCombo (const juce::String& id, const juce::String& name, const juce::String& tip)
{
    auto c = std::make_unique<Combo>();
    if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        int i = 1;
        for (auto& choice : cp->choices) c->c.addItem (choice, i++);
    }
    c->c.setTooltip (tip);
    addAndMakeVisible (c->c);
    c->l.setText (name, juce::dontSendNotification);
    c->l.setJustificationType (juce::Justification::centred);
    c->l.setFont (juce::FontOptions (11.0f));
    c->l.setColour (juce::Label::textColourId, ui::colours::dim);
    addAndMakeVisible (c->l);
    c->a = std::make_unique<ComboAtt> (proc.apvts, id, c->c);
    combos[id] = std::move (c);
}

void GlueForgeEditor::addToggle (const juce::String& id, const juce::String& name, const juce::String& tip)
{
    auto t = std::make_unique<Toggle>();
    t->b.setButtonText (name);
    t->b.setTooltip (tip);
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
    if (currentTab == 1) { mbView.tick(); return; } // multiband tab drives its own view

    inMeter.setLevelDb  (proc.getInputLevelDb());   inMeter.repaint();
    outMeter.setLevelDb (proc.getOutputLevelDb());  outMeter.repaint();
    const float gr = proc.getCurrentGainReductionDb();
    grMeter.setReductionDb (gr);                    grMeter.repaint();
    history.push (gr);                              history.repaint();

    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

    // Mode-aware centre display: transfer curve (compressor) vs pump shape (duck).
    const int trig = (int) raw (params::id::trigger);
    if (trig != lastTrigger)
    {
        lastTrigger = trig;
        const bool duck = (trig == 2);
        shapeEditor.setVisible (duck);
        curve.setVisible (! duck);
        displayCaption.setText (duck ? "PUMP SHAPE   (drag to move | double-click add/remove)"
                                     : "TRANSFER CURVE",
                                juce::dontSendNotification);
    }

    if (curve.isVisible())
    {
        curve.setParams (raw (params::id::threshold), raw (params::id::ratio), raw (params::id::knee));
        curve.setOperatingPoint (proc.getInputLevelDb(), gr);
        curve.repaint();
    }
    else
        shapeEditor.tick();
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

    r.removeFromTop (8);
    auto tabs = r.removeFromTop (26);
    compTabBtn.setBounds (tabs.removeFromLeft (120));
    tabs.removeFromLeft (4);
    mbTabBtn.setBounds (tabs.removeFromLeft (120));
    helpBtn.setBounds (tabs.removeFromRight (30));
    tabs.removeFromRight (6);
    manualBtn.setBounds (tabs.removeFromRight (74));
    r.removeFromTop (8);

    mbView.setBounds (r); // Multiband tab fills the body; visibility toggles per tab

    // --- Compressor page (shares the body region) ---
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
    auto curveCol = disp.removeFromLeft (disp.getWidth() / 2 - 4);
    displayCaption.setBounds (curveCol.removeFromTop (14));
    curve.setBounds (curveCol);       // compressor modes
    shapeEditor.setBounds (curveCol); // tempo-duck mode (same area, swapped by visibility)
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
    const juce::StringArray row5 { params::id::mix, params::id::lookahead, params::id::oversampling, params::id::midside };

    rowHeaders.clear();
    const int rh = r.getHeight() / 5;
    auto section = [&] (juce::Rectangle<int> rowArea, const juce::String& title, const juce::StringArray& ids)
    {
        rowHeaders.push_back ({ title, rowArea.removeFromTop (13) });
        layoutRow (rowArea, ids);
    };
    section (r.removeFromTop (rh), "DYNAMICS",               row1);
    section (r.removeFromTop (rh), "DETECTOR / OUTPUT",      row2);
    section (r.removeFromTop (rh), "SIDECHAIN",              row3);
    section (r.removeFromTop (rh), "TEMPO DUCK / CHARACTER", row4);
    section (r, "MIX / QUALITY", row5);
}

void GlueForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::bg);
    g.setColour (ui::colours::accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("GLUEFORGE", getLocalBounds().removeFromTop (32).removeFromRight (130),
                juce::Justification::centredRight, false);

    // Section headers + separators — only on the compressor page.
    if (currentTab == 0)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        for (auto& h : rowHeaders)
        {
            g.setColour (ui::colours::dim);
            g.drawText (h.first, h.second.reduced (4, 0), juce::Justification::centredLeft, false);
            g.setColour (ui::colours::track.withAlpha (0.5f));
            g.fillRect (h.second.getX(), h.second.getBottom() - 1, h.second.getWidth(), 1);
        }
    }
}

void GlueForgeEditor::savePresetToFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Nightshift Audio").getChildFile ("GlueForge").getChildFile ("Presets");
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

void GlueForgeEditor::openManual()
{
    // The manual PDF is embedded in the binary. Extract it to a temp file (once)
    // and hand it to the OS default PDF viewer.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GlueForge");
    dir.createDirectory();
    auto pdf = dir.getChildFile ("GlueForge-Manual.pdf");

    const auto* data = BinaryData::GlueForgeManual_pdf;
    const auto  size = (size_t) BinaryData::GlueForgeManual_pdfSize;
    if (! pdf.existsAsFile() || (size_t) pdf.getSize() != size)
        pdf.replaceWithData (data, size);

    pdf.startAsProcess();
}

void GlueForgeEditor::loadPresetFromFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Nightshift Audio").getChildFile ("GlueForge").getChildFile ("Presets");
    chooser = std::make_unique<juce::FileChooser> ("Load GlueForge preset", dir, "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{} || ! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
                if (xml->hasTagName (proc.apvts.state.getType()))
                {
                    proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
                    proc.rebuildShapeLut();
                }
        });
}
