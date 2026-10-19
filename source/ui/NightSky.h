#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>
#include <vector>

namespace overcast::ui
{

// The animated sky behind the controls: gradient, moon, twinkling stars,
// rare shooting stars and two layers of cloud rolling along the bottom.
//
// It's drawn by the panel rather than being a component, so the panel's own text
// sits on top of it. Everything is in panel units (1040 x 620). The expensive
// parts (gradient, moon, blurred clouds) are cached as images at the screen's
// pixel density and rebuilt only when that changes.
class NightSky
{
public:
    static constexpr float width = 1040.0f, height = 620.0f;

    NightSky();

    // Moves the animation to `seconds`. Shooting stars are scheduled at random.
    void advance (double seconds);

    // With motion off the sky shows a still frame: stars at rest, clouds parked.
    void setMotion (bool shouldMove) noexcept { motion = shouldMove; }

    void draw (juce::Graphics&);

private:
    struct Star { float x, y, size, opacity, glow, period, phase; };
    struct ShootingStar { double start; juce::Point<float> from; float length, angle; };
    struct Blob { float x, y, w, h; juce::Colour colour; float blur; };

    void rebuildCaches (float pixelScale);
    juce::Image renderBlobs (const std::vector<Blob>&, juce::Rectangle<float> area, float pixelsPerUnit, bool ellipses) const;
    void drawStars (juce::Graphics&) const;
    void drawShootingStar (juce::Graphics&, const ShootingStar&) const;

    std::vector<Star> stars;
    std::vector<Blob> cloudsA, cloudsB, wisps;
    std::optional<ShootingStar> shooting;
    double nextShootingStar = 0.0;
    double now = 0.0;
    bool motion = true;
    juce::Random random;

    float cachedScale = 0.0f;
    juce::Image backdrop, starSprite;
    std::array<juce::Image, 3> starCells;
    juce::Image cloudImageA, cloudImageB, wispImage;
    juce::Rectangle<float> cloudAreaA, cloudAreaB, wispArea;
};

} // namespace overcast::ui
