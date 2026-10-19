#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace overcast::ui
{

// A night sky knob: the 84-unit dial with its glowing dot, plus its label and
// value underneath. Drag up/down (hold Shift for fine moves), scroll, or
// double-click to reset.
class Knob final : public juce::Slider
{
public:
    static constexpr float dialSize = 84.0f;
    static constexpr float topMargin = 12.0f; // room for the dial's shadow

    Knob();

    void setLabel (const juce::String&);
    std::function<juce::String (double)> formatValue;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

private:
    juce::Rectangle<float> dialBounds() const;
    void rebuildDial (float pixelScale);

    juce::String label;
    juce::Image dial;
    float dialScale = 0.0f;
};

// One of the 12 machine buttons along the middle of the panel.
class MachineButton final : public juce::Button
{
public:
    explicit MachineButton (const juce::String& name);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// The rounded switches along the bottom: Freeze, Spillover, Kill Dry.
class PillToggle final : public juce::ToggleButton
{
public:
    PillToggle (const juce::String& text, juce::Colour onColour);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    juce::Colour getOnColour() const noexcept { return onColour; }

private:
    juce::Colour onColour;
};

// Small framed buttons in the header (A / B and Save).
class HeaderButton final : public juce::Button
{
public:
    explicit HeaderButton (const juce::String& text);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    // For A / B: which letter is lit (0 = A, 1 = B, -1 = neither).
    void setLitSide (int side) { litSide = side; repaint(); }

private:
    int litSide = -1;
};

// The preset arrows: a gold chevron.
class ArrowButton final : public juce::Button
{
public:
    ArrowButton (const juce::String& name, bool pointsLeft);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool left;
};

// Text that you drag up and down to change, like "LOW CUT  80 Hz".
class TextDragger final : public juce::Slider
{
public:
    explicit TextDragger (const juce::String& caption);
    std::function<juce::String (double)> formatValue;
    void paint (juce::Graphics&) override;
    float preferredWidth() const;

private:
    juce::String caption;
};

// Text that flips between two choices when clicked, like "MIX LAW  Pedal".
class TextToggle final : public juce::ToggleButton
{
public:
    TextToggle (const juce::String& caption, const juce::String& offText, const juce::String& onText);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    float preferredWidth() const;

private:
    juce::String caption, offText, onText;
};

// "Name this preset" box shown over the panel when Save is clicked.
class SaveOverlay final : public juce::Component
{
public:
    SaveOverlay();
    void show (const juce::String& suggestedName);
    std::function<void (const juce::String&)> onSave;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void commit();
    juce::Rectangle<float> boxBounds() const;

    juce::TextEditor name;
    HeaderButton save { "SAVE" }, cancel { "CANCEL" };
};

// Look and feel for the bits JUCE draws itself: popup menus, text editor, resizer.
class NightSkyLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NightSkyLookAndFeel();
    juce::Font getPopupMenuFont() override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

} // namespace overcast::ui
