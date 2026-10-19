#include "PluginEditor.h"
#include "ui/MachineInfo.h"
#include "ui/ReducedMotion.h"
#include "ui/Theme.h"

namespace overcast
{

using namespace ui;

namespace
{
    constexpr int kPanelWidth = 1040, kPanelHeight = 620;
    constexpr double kFrameInterval = 1.0 / 30.0; // 30 fps is plenty for slow clouds and twinkles

    float defaultValueOf (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        auto* p = state.getParameter (id);
        return p->convertFrom0to1 (p->getDefaultValue());
    }
}

//==============================================================================
// The preset number and name in the middle of the header. Click it for the full list.
class NightSkyPanel::PresetName final : public juce::Button
{
public:
    PresetName() : juce::Button ("Preset") { setTitle ("Preset list"); }

    void set (const juce::String& number, const juce::String& name, bool edited)
    {
        num = number;
        text = name;
        modified = edited;
        setDescription (name);
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto font = Fonts::sans (15.0f, false, 0.04f);
        const auto shown = text + (modified ? "*" : "");
        const auto numText = num.isEmpty() ? juce::String() : num + "  ";
        const float numW = juce::GlyphArrangement::getStringWidth (font, numText);
        const float total = numW + trackedTextWidth (shown, font);
        const float x = b.getCentreX() - total * 0.5f;

        drawTrackedText (g, numText, b.withX (x).withWidth (numW + 2.0f), font, juce::Justification::centredLeft, Colours::muted);
        drawTrackedText (g, shown, b.withX (x + numW).withRight (b.getRight()), font, juce::Justification::centredLeft,
                         highlighted ? Colours::ivory : Colours::text);
    }

private:
    juce::String num, text;
    bool modified = false;
};

//==============================================================================
NightSkyPanel::NightSkyPanel (OvercastProcessor& p)
    : processor (p), presets (p.getPresets()), presetName (std::make_unique<PresetName>())
{
    setOpaque (true);
    auto& state = processor.getState();

    // Header
    for (auto* c : std::initializer_list<juce::Component*> { &previous, &next, presetName.get(), &abButton, &saveButton })
        addAndMakeVisible (c);
    previous.onClick = [this] { presets.loadPrevious(); updatePresetDisplay(); };
    next.onClick = [this] { presets.loadNext(); updatePresetDisplay(); };
    presetName->onClick = [this] { showPresetMenu(); };
    abButton.setTooltip ("Compare two settings");
    abButton.onClick = [this] { presets.toggleAB(); updatePresetDisplay(); };
    saveButton.onClick = [this]
    {
        const auto name = presets.getCurrentIndex() >= presets.getNumFactoryPresets() ? presets.getCurrentName() : juce::String();
        saveOverlay.show (name);
    };

    // Machines
    for (int i = 0; i < numMachines(); ++i)
    {
        auto button = std::make_unique<MachineButton> (machineInfo (i).name);
        button->setRadioGroupId (1);
        button->onClick = [this, i] { machineAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        addAndMakeVisible (*button);
        machineButtons.push_back (std::move (button));
    }
    machineAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (ParamID::machine),
        [this] (float value) { setMachine (juce::roundToInt (value)); });

    // Knobs
    const char* ids[numKnobs] = { ParamID::decay, ParamID::preDelay, ParamID::mix, ParamID::tone,
                                  ParamID::mod, ParamID::param1, ParamID::param2 };
    for (int i = 0; i < numKnobs; ++i)
    {
        auto& knob = knobs[static_cast<size_t> (i)];
        addAndMakeVisible (knob);
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, ids[i], knob));
        knob.setDoubleClickReturnValue (true, defaultValueOf (state, ids[i]));
    }
    knobs[mix].setLabel ("Mix");
    knobs[mix].formatValue = [] (double v) { return formatPercent (static_cast<float> (v)); };
    knobs[tone].setLabel ("Tone");
    knobs[tone].formatValue = [] (double v) { return formatTone (static_cast<float> (v)); };
    knobs[mod].setLabel ("Mod");
    knobs[mod].formatValue = [] (double v) { return formatPercent (static_cast<float> (v)); };
    knobs[decay].formatValue = [this] (double v) { return formatDecay (machine, static_cast<float> (v)); };
    knobs[preDelay].formatValue = [this] (double v) { return formatPreDelay (machine, static_cast<float> (v)); };
    knobs[param1].formatValue = [this] (double v) { return formatParam1 (machine, static_cast<float> (v)); };
    knobs[param2].formatValue = [this] (double v) { return formatParam2 (machine, static_cast<float> (v)); };

    // Bottom row
    for (auto* c : std::initializer_list<juce::Component*> { &freeze, &spillover, &killDry, &lowCut, &highCut, &mixLaw })
        addAndMakeVisible (c);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::freeze, freeze));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::spillover, spillover));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::killDry, killDry));
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, ParamID::mixLaw, mixLaw));
    freeze.onStateChange = [this] { repaint(); }; // its glow is drawn by the panel
    freeze.setTooltip ("Hold the tail forever");
    spillover.setTooltip ("Let the old tail ring out when you change machine");
    killDry.setTooltip ("Wet signal only, for send/return use");

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, ParamID::lowCut, lowCut));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, ParamID::highCut, highCut));
    lowCut.setDoubleClickReturnValue (true, defaultValueOf (state, ParamID::lowCut));
    highCut.setDoubleClickReturnValue (true, defaultValueOf (state, ParamID::highCut));
    lowCut.formatValue = highCut.formatValue = [] (double v) { return formatFrequency (static_cast<float> (v)); };

    addChildComponent (saveOverlay);
    saveOverlay.onSave = [this] (const juce::String& name)
    {
        if (! presets.saveUserPreset (name))
        {
            presetNote = "Couldn't save the preset";
            presetNoteUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
        }
        updatePresetDisplay();
    };

    machineAttachment->sendInitialUpdate();
    updatePresetDisplay();
    setSize (kPanelWidth, kPanelHeight);
}

NightSkyPanel::~NightSkyPanel() = default;

void NightSkyPanel::setMachine (int index)
{
    machine = juce::jlimit (0, numMachines() - 1, index);
    for (int i = 0; i < numMachines(); ++i)
        machineButtons[static_cast<size_t> (i)]->setToggleState (i == machine, juce::dontSendNotification);

    const auto& info = machineInfo (machine);
    knobs[decay].setLabel (decayLabel (machine));
    knobs[preDelay].setLabel (preDelayLabel (machine));
    knobs[param1].setLabel (info.param1Label);
    knobs[param2].setLabel (info.param2Label);
    for (auto& k : knobs)
        k.repaint();
    repaint();
}

void NightSkyPanel::updatePresetDisplay()
{
    const bool edited = presets.isModified();
    const int index = presets.getCurrentIndex();
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    juce::String number, name;
    if (presetNote.isNotEmpty() && now < presetNoteUntil)
        name = presetNote;
    else
    {
        presetNote.clear();
        number = index >= 0 ? juce::String (index + 1).paddedLeft ('0', 3) : juce::String();
        name = presets.getCurrentName();
    }

    const auto key = number + "|" + name + "|" + (edited ? "*" : "");
    if (key != lastPresetText || presets.isOnB() != lastAB || presets.getChangeCount() != lastPresetChange)
    {
        lastPresetText = key;
        lastAB = presets.isOnB();
        lastPresetChange = presets.getChangeCount();
        presetName->set (number, name, edited && presetNote.isEmpty());
        abButton.setLitSide (presets.isOnB() ? 1 : 0);
    }
}

void NightSkyPanel::showPresetMenu()
{
    presets.refreshUserPresets();

    juce::PopupMenu menu;
    const int factoryCount = presets.getNumFactoryPresets();
    const int total = presets.getNumPresets();
    const int current = presets.getCurrentIndex();

    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < factoryCount; ++i)
        menu.addItem (i + 1, juce::String (i + 1).paddedLeft ('0', 3) + "   " + presets.getPresetName (i), true, i == current);

    menu.addSectionHeader ("YOURS");
    if (total == factoryCount)
        menu.addItem (-1, "Save a preset and it shows up here", false);
    for (int i = factoryCount; i < total; ++i)
        menu.addItem (i + 1, juce::String (i + 1).paddedLeft ('0', 3) + "   " + presets.getPresetName (i), true, i == current);

    menu.addSeparator();
    constexpr int kShowFolder = 10000;
    menu.addItem (kShowFolder, "Show presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName.get()).withMinimumWidth (260),
                        [safe = juce::Component::SafePointer<NightSkyPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == kShowFolder)
                            {
                                auto folder = PresetManager::getUserPresetFolder();
                                folder.createDirectory();
                                folder.revealToUser();
                                return;
                            }
                            safe->presets.loadPreset (result - 1);
                            safe->updatePresetDisplay();
                        });
}

void NightSkyPanel::frame (double seconds, bool animate)
{
    sky.setMotion (animate);
    sky.advance (seconds);
    if (animate)
        repaint();

    // The preset name changes when knobs move or the host loads a session.
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - lastPresetCheck > 0.2)
    {
        lastPresetCheck = now;
        updatePresetDisplay();
    }
}

void NightSkyPanel::resized()
{
    // Header: 24 units from the top, 48 tall, 40 from each side.
    constexpr float left = 40.0f, right = 1000.0f, headerY = 24.0f, headerH = 48.0f;
    const auto titleFont = Fonts::serif (30.0f, true, 0.32f);
    logoArea = { left, headerY, 26.0f + 12.0f + trackedTextWidth ("OVERCAST", titleFont) + 10.0f, headerH };

    auto buttonWidth = [] (const juce::String& t) { return 32.0f + trackedTextWidth (t, Fonts::sans (13.0f, false, 0.12f)) + 2.0f; };
    const float saveW = buttonWidth ("SAVE"), abW = buttonWidth ("A / B");
    saveButton.setBounds (juce::Rectangle<float> (right - saveW, headerY + 6.0f, saveW, 36.0f).toNearestInt());
    abButton.setBounds (juce::Rectangle<float> (right - saveW - 8.0f - abW, headerY + 6.0f, abW, 36.0f).toNearestInt());

    // The preset box sits midway between the logo and the buttons, like the mockup's flex row.
    constexpr float frameW = 2.0f + 6.0f + 44.0f + 8.0f + 240.0f + 8.0f + 44.0f + 6.0f;
    const float gapLeft = logoArea.getRight(), gapRight = static_cast<float> (abButton.getX());
    presetFrame = { gapLeft + (gapRight - gapLeft - frameW) * 0.5f, headerY + 1.0f, frameW, 46.0f };
    const auto inner = presetFrame.reduced (7.0f, 5.0f);
    previous.setBounds (juce::Rectangle<float> (inner.getX(), inner.getY(), 44.0f, 36.0f).toNearestInt());
    next.setBounds (juce::Rectangle<float> (inner.getRight() - 44.0f, inner.getY(), 44.0f, 36.0f).toNearestInt());
    presetName->setBounds (juce::Rectangle<float> (inner.getX() + 52.0f, inner.getY(), 240.0f, 36.0f).toNearestInt());

    // Machine buttons: 12 across, 6 apart, at y = 259.
    constexpr float contentW = right - left;
    const float colW = (contentW - 11.0f * 6.0f) / 12.0f;
    for (int i = 0; i < numMachines(); ++i)
        machineButtons[static_cast<size_t> (i)]->setBounds (
            juce::Rectangle<float> (left + static_cast<float> (i) * (colW + 6.0f), 259.0f, colW, 44.0f).toNearestInt());

    // Knobs: 7 across, 8 apart, dials starting at y = 337.
    const float knobW = (contentW - 6.0f * 8.0f) / 7.0f;
    for (int i = 0; i < numKnobs; ++i)
        knobs[static_cast<size_t> (i)].setBounds (
            juce::Rectangle<float> (left + static_cast<float> (i) * (knobW + 8.0f), 337.0f - Knob::topMargin,
                                    knobW, Knob::topMargin + Knob::dialSize + 64.0f).toNearestInt());

    // Bottom row, 28 from the bottom edge.
    constexpr float rowY = 620.0f - 28.0f - 40.0f;
    float x = left;
    for (auto* pill : { &freeze, &spillover, &killDry })
    {
        const float w = 36.0f + trackedTextWidth (pill->getButtonText(), Fonts::sans (12.0f, false, 0.2f)) + 2.0f;
        pill->setBounds (juce::Rectangle<float> (x, rowY, w, 40.0f).toNearestInt());
        x += w + 10.0f;
    }
    float rx = right;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &highCut, &lowCut, &mixLaw })
    {
        const float w = c == &mixLaw ? mixLaw.preferredWidth()
                      : (c == &lowCut ? lowCut.preferredWidth() : highCut.preferredWidth());
        c->setBounds (juce::Rectangle<float> (rx - w, rowY, w, 40.0f).toNearestInt());
        rx -= w + 24.0f;
    }

    saveOverlay.setBounds (getLocalBounds());
}

void NightSkyPanel::paint (juce::Graphics& g)
{
    sky.draw (g);

    // Logo: a crescent moon and the name.
    {
        static const juce::Path crescent = juce::Drawable::parseSVGPath ("M17 3a10 10 0 1 0 6 17A11 11 0 0 1 17 3z");
        g.setColour (Colours::cream);
        g.fillPath (crescent, juce::AffineTransform::translation (logoArea.getX(), logoArea.getCentreY() - 13.0f));
        drawTrackedText (g, "OVERCAST", logoArea.withTrimmedLeft (38.0f), Fonts::serif (30.0f, true, 0.32f),
                         juce::Justification::centredLeft, Colours::cream);
    }

    // Preset box frame.
    g.setColour (Colours::night.withAlpha (0.55f));
    g.fillRoundedRectangle (presetFrame, 12.0f);
    g.setColour (Colours::gold.withAlpha (0.35f));
    g.drawRoundedRectangle (presetFrame.reduced (0.5f), 12.0f, 1.0f);

    // Machine name and description.
    const auto& info = machineInfo (machine);
    drawTrackedText (g, "MACHINE", { 40.0f, 128.0f, 600.0f, 16.0f }, Fonts::sans (12.0f, false, 0.28f),
                     juce::Justification::centredLeft, Colours::muted);
    drawTrackedText (g, info.name, { 40.0f, 150.0f, 600.0f, 55.0f }, Fonts::serif (52.0f),
                     juce::Justification::centredLeft, Colours::ivory);
    drawTrackedText (g, info.description, { 40.0f, 211.0f, 600.0f, 20.0f }, Fonts::sans (15.0f),
                     juce::Justification::centredLeft, Colours::soft);

    // Glows sit under their buttons, like CSS box-shadows.
    if (machine >= 0 && machine < static_cast<int> (machineButtons.size()))
        drawGlow (g, machineButtons[static_cast<size_t> (machine)]->getBounds().toFloat(), 10.0f, 14.0f,
                  Colours::gold.withAlpha (0.45f));
    if (freeze.getToggleState())
        drawGlow (g, freeze.getBounds().toFloat(), 20.0f, 16.0f, Colours::ice.withAlpha (0.6f));
}

//==============================================================================
OvercastEditor::OvercastEditor (OvercastProcessor& p)
    : AudioProcessorEditor (p), processor (p), panel (p),
      vblank (this, [this] (double t) { onFrame (t); })
{
    // Read the saved size first: setting the limits below calls resized(), which stores a new one.
    const float scale = juce::jlimit (0.7f, 1.5f, processor.getUiScale());

    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kPanelWidth * 7 / 10, kPanelHeight * 7 / 10, kPanelWidth * 3 / 2, kPanelHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kPanelWidth) / kPanelHeight);

    motion = ! prefersReducedMotion();
    panel.frame (0.0, motion);
    setSize (juce::roundToInt (kPanelWidth * scale), juce::roundToInt (kPanelHeight * scale));
}

OvercastEditor::~OvercastEditor()
{
    setLookAndFeel (nullptr);
}

void OvercastEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / kPanelWidth;
    panel.setTransform (juce::AffineTransform::scale (scale));
    processor.setUiScale (scale);
}

void OvercastEditor::onFrame (double timestamp)
{
    if (lastFrame < 0.0)
        lastFrame = timestamp;
    const double dt = timestamp - lastFrame;
    if (dt < kFrameInterval - 0.004)
        return;
    lastFrame = timestamp;

    // Check the accessibility setting about once a second; it's cheap but not free.
    if (timestamp - lastMotionCheck > 1.0)
    {
        lastMotionCheck = timestamp;
        const bool wanted = ! prefersReducedMotion();
        if (wanted != motion)
        {
            motion = wanted;
            panel.repaint();
        }
    }

    // Pause the clock rather than jump ahead after the window was hidden.
    if (motion)
        animationTime += std::min (dt, 0.1);
    panel.frame (animationTime, motion);
}

} // namespace overcast
