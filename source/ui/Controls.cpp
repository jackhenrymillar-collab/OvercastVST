#include "Controls.h"
#include "Theme.h"

#include <cmath>

namespace overcast::ui
{

//==============================================================================
Knob::Knob()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (220);
    // Holding Shift switches to velocity mode with a low sensitivity, for fine moves.
    setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setScrollWheelEnabled (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
    onValueChange = [this] { repaint(); };
}

void Knob::setLabel (const juce::String& newLabel)
{
    if (label != newLabel)
    {
        label = newLabel;
        setTitle (newLabel);
        repaint();
    }
}

juce::Rectangle<float> Knob::dialBounds() const
{
    return { (static_cast<float> (getWidth()) - dialSize) * 0.5f, topMargin, dialSize, dialSize };
}

bool Knob::hitTest (int x, int y)
{
    // The dial and its text both respond, but not the empty space beside them.
    const auto d = dialBounds();
    const auto p = juce::Point<float> (static_cast<float> (x), static_cast<float> (y));
    return p.getDistanceFrom (d.getCentre()) <= dialSize * 0.55f
        || (p.y > d.getBottom() && std::abs (p.x - d.getCentreX()) < dialSize * 0.7f);
}

void Knob::rebuildDial (float pixelScale)
{
    dialScale = pixelScale;
    dial = juce::Image (juce::Image::ARGB,
                        std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * pixelScale)),
                        std::max (1, juce::roundToInt ((topMargin + dialSize + 40.0f) * pixelScale)), true);
    juce::Graphics g (dial);
    g.addTransform (juce::AffineTransform::scale (pixelScale));

    const auto d = dialBounds();
    const auto c = d.getCentre();
    const float r = dialSize * 0.5f;

    // box-shadow: 0 8px 18px rgba(0,0,0,0.45)
    fillBlurredDisc (g, c.translated (0.0f, 8.0f), r, 18.0f, juce::Colours::black.withAlpha (0.45f));

    // radial-gradient(circle at 36% 30%, #4a5490, #262e62 55%, #141a40)
    const juce::Point<float> light (d.getX() + 0.36f * dialSize, d.getY() + 0.30f * dialSize);
    juce::ColourGradient body (juce::Colour (0xff4a5490), light, juce::Colour (0xff141a40), light.translated (79.7f, 0.0f), true);
    body.addColour (0.55, juce::Colour (0xff262e62));
    g.setGradientFill (body);
    g.fillEllipse (d);

    // inset 0 1px 0 rgba(255,255,255,0.12): a thin highlight along the top edge.
    {
        juce::Path crescent;
        crescent.addEllipse (d);
        crescent.addEllipse (d.translated (0.0f, 1.0f));
        crescent.setUsingNonZeroWinding (false);
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (d);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillPath (crescent);
    }
}

void Knob::paint (juce::Graphics& g)
{
    const float pixelScale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (pixelScale - dialScale) > 0.01f || ! dial.isValid())
        rebuildDial (pixelScale);
    g.drawImageTransformed (dial, juce::AffineTransform::scale (1.0f / dialScale));

    const auto d = dialBounds();
    const auto c = d.getCentre();

    // The 1px gold ring, brighter while you're touching the knob.
    g.setColour (Colours::gold.withAlpha (isMouseOverOrDragging() ? 0.7f : 0.35f));
    g.drawEllipse (d.expanded (0.5f), 1.0f);

    // The glowing dot sweeps 270 degrees, from -135 to +135.
    const float angle = juce::degreesToRadians (-135.0f + 270.0f * static_cast<float> (valueToProportionOfLength (getValue())));
    const juce::Point<float> dot (c.x + 31.0f * std::sin (angle), c.y - 31.0f * std::cos (angle));
    fillBlurredDisc (g, dot, 5.0f, 8.0f, Colours::cream.withAlpha (0.8f));
    g.setColour (Colours::cream);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));

    const float w = static_cast<float> (getWidth());
    drawTrackedText (g, label.toUpperCase(), { 0.0f, d.getBottom() + 10.0f, w, 16.0f },
                     Fonts::sans (12.0f, false, 0.2f), juce::Justification::centred, Colours::soft);
    const auto value = formatValue ? formatValue (getValue()) : getTextFromValue (getValue());
    drawTrackedText (g, value, { 0.0f, d.getBottom() + 36.0f, w, 20.0f },
                     Fonts::sans (14.0f), juce::Justification::centred, Colours::cream);
}

//==============================================================================
MachineButton::MachineButton (const juce::String& name) : juce::Button (name)
{
    setTitle (name);
    setClickingTogglesState (false);
}

void MachineButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();

    if (on)
        g.setColour (Colours::gold.withAlpha (down ? 0.28f : 0.2f));
    else
        g.setColour (highlighted ? juce::Colour (0xff1a2150).withAlpha (0.6f) : Colours::glass);
    g.fillRoundedRectangle (b, 10.0f);

    g.setColour (on ? Colours::gold : Colours::text.withAlpha (highlighted ? 0.35f : 0.18f));
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    drawTrackedText (g, getName(), b, Fonts::sans (12.0f, false, 0.04f), juce::Justification::centred,
                     on ? Colours::ivory : (highlighted ? Colours::text : Colours::soft));
}

//==============================================================================
PillToggle::PillToggle (const juce::String& caption, juce::Colour on)
    : onColour (on)
{
    setButtonText (caption);
    setTitle (caption);
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = b.getHeight() * 0.5f;
    const bool on = getToggleState();

    if (on)
        g.setColour (onColour.withAlpha (down ? 0.32f : 0.22f));
    else
        g.setColour (highlighted ? juce::Colour (0xff1a2150).withAlpha (0.6f) : Colours::glass);
    g.fillRoundedRectangle (b, radius);

    g.setColour (on ? onColour.withAlpha (0.85f) : Colours::hairline.withMultipliedAlpha (highlighted ? 1.5f : 1.0f));
    g.drawRoundedRectangle (b, radius, 1.0f);

    drawTrackedText (g, getButtonText(), b, Fonts::sans (12.0f, false, 0.2f), juce::Justification::centred,
                     on ? Colours::ivory : Colours::soft);
}

//==============================================================================
HeaderButton::HeaderButton (const juce::String& caption) : juce::Button (caption)
{
    setButtonText (caption);
    setTitle (caption);
}

void HeaderButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::night.withAlpha (down ? 0.65f : (highlighted ? 0.55f : 0.4f)));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Colours::text.withAlpha (highlighted ? 0.4f : 0.25f));
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    const auto font = Fonts::sans (13.0f, false, 0.12f);
    if (litSide < 0)
    {
        drawTrackedText (g, getButtonText(), b, font, juce::Justification::centred, Colours::text);
        return;
    }

    // "A / B" with the active side lit in gold.
    const juce::String parts[] = { "A", " / ", "B" };
    float total = 0.0f;
    float widths[3];
    for (int i = 0; i < 3; ++i)
        total += widths[i] = juce::GlyphArrangement::getStringWidth (font, parts[i]);
    total -= font.getHeight() * font.getExtraKerningFactor();

    float x = b.getCentreX() - total * 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        const auto colour = i == 1 ? Colours::muted
                          : ((i == 0) == (litSide == 0) ? Colours::gold : Colours::muted);
        drawTrackedText (g, parts[i], { x, b.getY(), widths[i] + 4.0f, b.getHeight() }, font,
                         juce::Justification::centredLeft, colour);
        x += widths[i];
    }
}

//==============================================================================
ArrowButton::ArrowButton (const juce::String& name, bool pointsLeft)
    : juce::Button (name), left (pointsLeft)
{
    setTitle (name);
}

void ArrowButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    if (highlighted || down)
    {
        g.setColour (Colours::gold.withAlpha (down ? 0.18f : 0.1f));
        g.fillRoundedRectangle (b, 8.0f);
    }

    const auto c = b.getCentre();
    const float s = left ? 1.0f : -1.0f;
    juce::Path chevron;
    chevron.startNewSubPath (c.x + 2.5f * s, c.y - 5.0f);
    chevron.lineTo (c.x - 2.5f * s, c.y);
    chevron.lineTo (c.x + 2.5f * s, c.y + 5.0f);
    g.setColour (Colours::gold);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
TextDragger::TextDragger (const juce::String& captionText) : caption (captionText)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (200);
    setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (true);
    setTitle (caption);
    onValueChange = [this] { repaint(); };
}

float TextDragger::preferredWidth() const
{
    // Wide enough for the longest value, so the text doesn't shift as it changes.
    const auto font = Fonts::sans (12.0f, false, 0.16f);
    return trackedTextWidth (caption.toUpperCase() + "  ", font) + trackedTextWidth ("20.0 kHz", font) + 4.0f;
}

void TextDragger::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto font = Fonts::sans (12.0f, false, 0.16f);
    const auto cap = caption.toUpperCase() + "  ";
    const float capWidth = juce::GlyphArrangement::getStringWidth (font, cap);
    drawTrackedText (g, cap, b, font, juce::Justification::centredLeft,
                     isMouseOverOrDragging() ? Colours::text : Colours::soft);
    const auto value = formatValue ? formatValue (getValue()) : getTextFromValue (getValue());
    drawTrackedText (g, value, b.withTrimmedLeft (capWidth), font, juce::Justification::centredLeft,
                     isMouseOverOrDragging() ? Colours::ivory : Colours::cream);
}

//==============================================================================
TextToggle::TextToggle (const juce::String& captionText, const juce::String& off, const juce::String& on)
    : caption (captionText), offText (off), onText (on)
{
    setTitle (caption);
}

float TextToggle::preferredWidth() const
{
    const auto font = Fonts::sans (12.0f, false, 0.16f);
    return trackedTextWidth (caption.toUpperCase() + "  ", font)
         + std::max (trackedTextWidth (offText, font), trackedTextWidth (onText, font)) + 4.0f;
}

void TextToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const auto font = Fonts::sans (12.0f, false, 0.16f);
    const auto cap = caption.toUpperCase() + "  ";
    const float capWidth = juce::GlyphArrangement::getStringWidth (font, cap);
    drawTrackedText (g, cap, b, font, juce::Justification::centredLeft, highlighted ? Colours::text : Colours::soft);
    drawTrackedText (g, getToggleState() ? onText : offText, b.withTrimmedLeft (capWidth), font,
                     juce::Justification::centredLeft, highlighted ? Colours::ivory : Colours::cream);
}

//==============================================================================
SaveOverlay::SaveOverlay()
{
    setInterceptsMouseClicks (true, true);
    addAndMakeVisible (name);
    addAndMakeVisible (save);
    addAndMakeVisible (cancel);

    name.setFont (Fonts::sans (15.0f));
    name.setJustification (juce::Justification::centredLeft);
    name.setIndents (12, 0);
    name.setColour (juce::TextEditor::backgroundColourId, Colours::night.withAlpha (0.7f));
    name.setColour (juce::TextEditor::textColourId, Colours::ivory);
    name.setColour (juce::TextEditor::outlineColourId, Colours::gold.withAlpha (0.35f));
    name.setColour (juce::TextEditor::focusedOutlineColourId, Colours::gold.withAlpha (0.8f));
    name.setColour (juce::TextEditor::highlightColourId, Colours::gold.withAlpha (0.3f));
    name.setColour (juce::CaretComponent::caretColourId, Colours::gold);
    name.setInputRestrictions (60);
    name.setTitle ("Preset name");
    name.onReturnKey = [this] { commit(); };
    name.onEscapeKey = [this] { setVisible (false); };

    save.onClick = [this] { commit(); };
    cancel.onClick = [this] { setVisible (false); };
    setVisible (false);
}

void SaveOverlay::show (const juce::String& suggestedName)
{
    name.setText (suggestedName, false);
    name.selectAll();
    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
}

void SaveOverlay::commit()
{
    const auto text = name.getText().trim();
    if (text.isEmpty())
        return;
    setVisible (false);
    if (onSave)
        onSave (text);
}

juce::Rectangle<float> SaveOverlay::boxBounds() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (380.0f, 176.0f);
}

void SaveOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Colours::night.withAlpha (0.6f));
    const auto box = boxBounds();
    g.setColour (juce::Colour (0xff0f1536).withAlpha (0.97f));
    g.fillRoundedRectangle (box, 14.0f);
    g.setColour (Colours::gold.withAlpha (0.35f));
    g.drawRoundedRectangle (box, 14.0f, 1.0f);
    drawTrackedText (g, "SAVE PRESET", box.reduced (24.0f).withHeight (16.0f),
                     Fonts::sans (12.0f, false, 0.28f), juce::Justification::centredLeft, Colours::muted);
}

void SaveOverlay::resized()
{
    const auto box = boxBounds().reduced (24.0f).toNearestInt();
    name.setBounds (box.getX(), box.getY() + 28, box.getWidth(), 40);
    save.setBounds (box.getRight() - 90, box.getBottom() - 36, 90, 36);
    cancel.setBounds (box.getRight() - 90 - 10 - 100, box.getBottom() - 36, 100, 36);
}

void SaveOverlay::mouseDown (const juce::MouseEvent& e)
{
    // Clicking outside the box cancels.
    if (! boxBounds().contains (e.position))
        setVisible (false);
}

//==============================================================================
NightSkyLookAndFeel::NightSkyLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff0f1536));
    setColour (juce::PopupMenu::textColourId, Colours::soft);
    setColour (juce::PopupMenu::headerTextColourId, Colours::muted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::gold.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::ivory);
}

juce::Font NightSkyLookAndFeel::getPopupMenuFont() { return Fonts::sans (15.0f); }

void NightSkyLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (Colours::gold.withAlpha (0.3f));
    g.drawRect (0, 0, width, height, 1);
}

void NightSkyLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const float alpha = (isMouseOver || isMouseDragging) ? 0.6f : 0.25f;
    g.setColour (Colours::cream.withAlpha (alpha));
    const auto fw = static_cast<float> (w), fh = static_cast<float> (h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine (fw * i, fh, fw, fh * i, 1.0f);
}

} // namespace overcast::ui
