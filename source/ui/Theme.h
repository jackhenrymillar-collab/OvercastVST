#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours, fonts and drawing helpers for the night sky panel.
// Sizes are in panel units: the panel is laid out at 1040 x 620 and scaled as a whole.
namespace overcast::ui
{

namespace Colours
{
    inline const juce::Colour gold      { 0xffe8c77a };
    inline const juce::Colour cream     { 0xfff3ebd3 };
    inline const juce::Colour ivory     { 0xfffffaf0 };
    inline const juce::Colour text      { 0xffe6e9f5 };
    inline const juce::Colour soft      { 0xffc3c9e4 };
    inline const juce::Colour muted     { 0xff9aa3c7 };
    inline const juce::Colour ice       { 0xffbcd4ff };
    inline const juce::Colour night     { 0xff060918 };
    inline const juce::Colour glass     = juce::Colour (0xff060918).withAlpha (0.45f);
    inline const juce::Colour hairline  = juce::Colour (0xffe6e9f5).withAlpha (0.25f);
}

// The two typefaces from the mockup, embedded so the panel looks the same on every Mac.
// Both are under the SIL Open Font License (see resources/fonts).
class Fonts
{
public:
    Fonts();

    // letterSpacing is in ems, like CSS letter-spacing.
    static juce::Font serif (float px, bool semiBold = false, float letterSpacing = 0.0f);
    static juce::Font sans (float px, bool medium = false, float letterSpacing = 0.0f);

private:
    static juce::Font make (juce::Typeface::Ptr, float px, float letterSpacing);

    juce::Typeface::Ptr serifMedium, serifSemiBold, sansRegular, sansMedium;
};

// A soft disc like a CSS box-shadow: a circle of `radius` blurred with `blur`
// (the CSS blur length, so the edge softens over about that distance).
void fillBlurredDisc (juce::Graphics&, juce::Point<float> centre, float radius, float blur, juce::Colour);

// Same idea for a rounded rectangle glow, drawn as a few soft strokes. Cheap enough per frame.
void drawGlow (juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius, float blur, juce::Colour);

// In-place Gaussian blur of an ARGB image. sigma is in pixels of that image.
void gaussianBlur (juce::Image&, float sigma);

// Draws text with CSS-style letter spacing, without the trailing gap the spacing adds.
void drawTrackedText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&,
                      juce::Justification, juce::Colour);
float trackedTextWidth (const juce::String&, const juce::Font&);

} // namespace overcast::ui
