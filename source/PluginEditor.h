#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/NightSky.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace overcast
{

// The night sky front panel, laid out at 1040 x 620 units to match the mockup.
// The editor scales it as a whole when the window is resized.
class NightSkyPanel final : public juce::Component
{
public:
    explicit NightSkyPanel (OvercastProcessor&);
    ~NightSkyPanel() override;

    // Called once per display frame. `seconds` is the animation clock.
    void frame (double seconds, bool animate);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class PresetName;
    class MoonButton;

    void setMachine (int index);
    void showPresetMenu();
    void updatePresetDisplay();

    OvercastProcessor& processor;
    PresetManager& presets;
    ui::NightSky sky;
    int machine = 0;

    // Header
    ui::ArrowButton previous { "Previous preset", true }, next { "Next preset", false };
    std::unique_ptr<PresetName> presetName;
    ui::HeaderButton abButton { "A / B" }, saveButton { "SAVE" };
    juce::Rectangle<float> presetFrame, logoArea;

    // Machine picker and knobs
    std::vector<std::unique_ptr<ui::MachineButton>> machineButtons;
    std::unique_ptr<juce::ParameterAttachment> machineAttachment;

    enum KnobId { decay, preDelay, mix, tone, mod, param1, param2, numKnobs };
    std::array<ui::Knob, numKnobs> knobs;

    // Bottom row
    ui::PillToggle freeze { "FREEZE", ui::Colours::ice };
    ui::PillToggle spillover { "SPILLOVER", ui::Colours::gold };
    ui::PillToggle killDry { "KILL DRY", ui::Colours::gold };
    ui::TextDragger lowCut { "Low cut" }, highCut { "High cut" };
    ui::TextToggle mixLaw { "Mix law", "Equal", "Pedal" };

    ui::SaveOverlay saveOverlay;

    // The moon is the bypass switch.
    std::unique_ptr<MoonButton> moonButton;
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;
    bool bypassed = false;
    ui::NightSky::Inputs lastInputs;
    void setBypassed (bool);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    int lastPresetChange = -1;
    juce::String lastPresetText;
    bool lastAB = false;
    double lastPresetCheck = 0.0;
    juce::String presetNote;
    double presetNoteUntil = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NightSkyPanel)
};

class OvercastEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OvercastEditor (OvercastProcessor&);
    ~OvercastEditor() override;

    void resized() override;

    // Draws the panel at a given animation time. For the snapshot tool.
    void renderFrame (double seconds, bool animate) { panel.frame (seconds, animate); }

private:
    void onFrame (double timestamp);

    OvercastProcessor& processor;
    ui::NightSkyLookAndFeel lookAndFeel;
    NightSkyPanel panel;
    juce::VBlankAttachment vblank;

    double lastFrame = -1.0, animationTime = 0.0, lastMotionCheck = -10.0;
    bool motion = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvercastEditor)
};

} // namespace overcast
