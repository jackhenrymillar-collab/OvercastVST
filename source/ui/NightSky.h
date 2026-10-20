#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace overcast::ui
{

// The animated sky behind the controls: gradient, moon, twinkling stars, rare
// shooting stars, rolling clouds, and a theme for each machine (lantern light for
// Chamber, rain for Coil, a sunrise for Rise, northern lights for Aurora and so on).
//
// Switching machine cross-fades the themes over 3 s and glides the moon to that
// machine's phase. Bypass leaves a new moon over a pitch-black, stars-only sky.
// Tone tints the moon; Halo's Shimmer and Blossom's Feedback fill their skies in.
//
// It's drawn by the panel rather than being a component, so the panel's own text
// sits on top of it. Everything is in panel units (1040 x 620). The expensive
// parts (gradients, blurred shapes) are cached as images and rebuilt only when
// the screen's pixel density changes.
class NightSky
{
public:
    static constexpr float width = 1040.0f, height = 620.0f;
    static constexpr int numMachines = 12;

    // What the sky shows. The panel fills this in every frame.
    struct Inputs
    {
        int machine = 2;
        bool bypass = false;
        float tone = 0.0f;      // -1 (blood moon) .. 0 (cream) .. 1 (white)
        float shimmer = 0.5f;   // Halo's Shimmer knob, 0..1
        float feedback = 0.5f;  // Blossom's Feedback knob, 0..1
        bool freeze = false;    // Freeze holds the twinkling still
    };

    NightSky();

    void setInputs (const Inputs&);

    // Moves the animation to `seconds`. Shooting stars are scheduled at random.
    void advance (double seconds);

    // With motion off the sky shows a still frame and theme changes are instant.
    void setMotion (bool shouldMove) noexcept { motion = shouldMove; }

    void draw (juce::Graphics&);

    // The moon's disc, in panel units. The panel puts the bypass button here.
    static juce::Rectangle<float> moonBounds() noexcept { return { 812.0f, 64.0f, 150.0f, 150.0f }; }

    // How far each machine's moon is through its cycle: 0 new, 0.5 full (Halo).
    static float phaseFor (int machine) noexcept;

private:
    // A value that eases from one target to the next over a set time.
    struct Fade
    {
        float from = 0.0f, to = 0.0f;
        double start = 0.0, length = 3.0, delay = 0.0;
        void set (float target, double now, bool instant);
        float at (double now) const;
        bool settled (double now) const { return juce::exactlyEqual (from, to) || now >= start + delay + length; }
    };

    // One twinkling (or still) point of light.
    struct Dot
    {
        float x, y, size, opacity, glow;
        juce::Colour colour;
        float period = 0.0f, delay = 0.0f; // period 0: no twinkle
        float level = 0.0f;                // shows once its knob passes this
    };

    // Something that rises from the bottom (Blossom's lights and motes).
    struct Riser
    {
        float x, y, dx, size, period, delay, pulse, pulseDelay, level;
        juce::Colour colour, glow;
        bool smallGlow;
        bool eased;
    };

    struct Sparkle { float x, y, size, period, delay, level; juce::Colour colour; };
    struct RainDrop { float x, length, opacity, period, delay; };
    struct ShootingStar { double start; juce::Point<float> from; float length, angle; };
    struct Blob { float x, y, w, h; juce::Colour colour; float blur; float rotation = 0.0f; };
    struct Cached { juce::Image image; juce::Rectangle<float> area; float resolution = 1.0f; };

    enum Machine { chamber, arena, sheet, coil, rise, blossom, haze, aurora, halo, nebula, clearNight, spaceMap };

    void buildScene();
    void rebuildCaches (float pixelScale);
    Cached renderLayer (juce::Rectangle<float> area, float resolution, float blur, const std::function<void (juce::Graphics&)>&) const;
    Cached renderBlobs (const std::vector<Blob>&, juce::Rectangle<float> area, float resolution, bool ellipses) const;
    void drawCached (juce::Graphics&, const Cached&, float opacity, juce::AffineTransform extra = {}) const;

    // Once a layer has stopped fading in it's redrawn from a copy at screen
    // resolution, which is much cheaper than scaling up the small cached image.
    void drawAtRest (juce::Graphics&, const Cached&, float opacity, juce::Point<float> offset = {});
    void drawBase (juce::Graphics&);
    bool atRest (int machine) const { return themeFade[static_cast<size_t> (machine)].settled (now); }

    const juce::Image& dotSprite (float size, float glow);
    void drawDot (juce::Graphics&, const Dot&, float alpha, double clock);
    void drawDots (juce::Graphics&, const std::vector<Dot>&, float alpha, double clock, float knob = 2.0f);
    void drawSparkle (juce::Graphics&, juce::Point<float> centre, float size, float u, juce::Colour, float alpha) const;
    void drawRiser (juce::Graphics&, const Riser&, float alpha);
    void drawShootingStar (juce::Graphics&, const ShootingStar&) const;
    void drawMoon (juce::Graphics&, float alpha);
    void rebuildMoon (int toneStep);

    float themeAlpha (int machine) const { return themeFade[static_cast<size_t> (machine)].at (now); }

    // Scene contents, built once from fixed seeds so the sky always looks the same.
    std::vector<Dot> stars, dust, stillStars, deepStars, shimmerStars;
    std::vector<Sparkle> glitter, moonGlints;
    std::vector<Riser> lights, motes;
    std::vector<RainDrop> rain;
    std::vector<Blob> cloudsA, cloudsB, wisps, storm, nebulaBlobs;
    std::vector<juce::Point<float>> milkyDots;
    std::vector<float> milkySizes, milkyAlphas;

    std::optional<ShootingStar> shooting;
    double nextShootingStar = 0.0;
    double now = 0.0, twinkleClock = 0.0, lastAdvance = 0.0;
    bool motion = true, started = false;
    juce::Random random;

    // What the sky is heading towards, and the fades taking it there.
    Inputs inputs;
    int shownMachine = -1;
    bool shownBypass = false;
    std::array<Fade, numMachines> themeFade;
    Fade moonAlpha, clouds, cloudDrop, dark, starAlpha, starBoost, bypassFade;
    float phaseFrom = 0.0f, phaseDelta = 0.0f;
    double phaseStart = -10.0;
    float shimmerLevel = 0.5f, feedbackLevel = 0.5f;

    // Caches
    float cachedScale = 0.0f;
    int moonTone = 1000;
    juce::Image backdrop, moonDisc, moonGlow;
    std::map<int, juce::Image> sprites;
    juce::Image streakSprite, sparkleSprite;
    Cached cloudImageA, cloudImageB, wispImage, darkSky, sheetTint, frost, milkyWay, haloRing,
           lantern, lanternFlicker, sunrise, hazeTint, coilShade, stormImage;
    std::vector<Cached> nebulaImages, auroraImages, mistImages;

    // Screen-resolution copies for layers at rest, and the flattened base of the sky.
    std::map<const Cached*, juce::Image> restImages;
    juce::Image baseImage;
    std::array<float, 3> baseKey { -1.0f, -1.0f, -1.0f };
};

} // namespace overcast::ui
