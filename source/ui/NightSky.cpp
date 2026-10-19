#include "NightSky.h"
#include "Theme.h"

#include <cmath>

namespace overcast::ui
{

namespace
{
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    // Timings from the approved mockup.
    constexpr double kCloudPeriod = 12.0;   // both cloud layers loop every 12 s
    constexpr float  kTileA = 520.0f, kTileB = 312.0f;
    constexpr double kWispPeriod = 6.0;     // the wisps by the moon drift back and forth
    constexpr double kShootDuration = 0.75;
    constexpr float  kShootTravel = 404.0f;

    // Cloud blobs are blurred heavily, so they're cached at a quarter of panel
    // resolution and scaled up. Nobody can tell, and it keeps the blur cheap.
    constexpr float kCloudResolution = 0.25f;
    constexpr float kWispResolution = 0.5f;

    float easeInOut (float t) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t); }

    int spriteIndex (float size) { return size >= 3.0f ? 2 : (size >= 2.0f ? 1 : 0); }
    constexpr float kSpriteSize[3] = { 1.0f, 2.0f, 3.0f };
    constexpr float kSpriteGlow[3] = { 3.0f, 3.0f, 8.0f };
    constexpr float kSpriteCell = 32.0f; // panel units per sprite cell, big enough for the widest glow
}

NightSky::NightSky()
{
    // The same seeded star field as the mockup, so the panel matches it exactly.
    std::int64_t seed = 7;
    auto rnd = [&seed]
    {
        seed = (seed * 16807) % 2147483647;
        return static_cast<float> (static_cast<double> (seed - 1) / 2147483646.0);
    };
    constexpr float periods[] = { 2.0f, 3.0f, 4.0f, 6.0f };
    for (int i = 0; i < 90; ++i)
    {
        Star s {};
        const bool big = rnd() > 0.9f;
        s.x = std::round (rnd() * 1040.0f);
        s.y = std::round (rnd() * 500.0f);
        s.size = big ? 3.0f : (rnd() > 0.5f ? 2.0f : 1.0f);
        s.opacity = 0.35f + rnd() * 0.6f;
        s.glow = big ? 8.0f : 3.0f;
        s.period = periods[static_cast<int> (rnd() * 4.0f)];
        s.phase = rnd() * s.period;
        stars.push_back (s);
    }

    auto tile = [] (std::vector<Blob>& out, float tileWidth, int count, std::initializer_list<Blob> blobs)
    {
        for (int k = 0; k < count; ++k)
            for (auto b : blobs)
            {
                b.x += static_cast<float> (k) * tileWidth;
                b.y += 500.0f; // the cloud layers sit 500 units down
                out.push_back (b);
            }
    };
    const juce::Colour a1 (120, 132, 190), a2 (150, 160, 215), a3 (210, 216, 245);
    tile (cloudsA, kTileA, 4, { { -40.0f, 20.0f, 380.0f, 150.0f, a1.withAlpha (0.32f), 22.0f },
                                { 230.0f, 50.0f, 330.0f, 130.0f, a2.withAlpha (0.26f), 24.0f },
                                { 120.0f, 60.0f, 220.0f,  70.0f, a3.withAlpha (0.16f), 12.0f } });
    const juce::Colour b1 (110, 122, 180), b2 (170, 178, 225);
    tile (cloudsB, kTileB, 6, { { 0.0f,   0.0f, 300.0f, 120.0f, b1.withAlpha (0.22f), 26.0f },
                                { 150.0f, 30.0f, 240.0f,  90.0f, b2.withAlpha (0.14f), 18.0f } });

    wisps = { { 740.0f, 150.0f, 260.0f, 54.0f, juce::Colour (205, 212, 240).withAlpha (0.24f), 10.0f },
              { 860.0f, 128.0f, 150.0f, 46.0f, juce::Colour (220, 226, 250).withAlpha (0.20f), 8.0f } };

    // Cloud layers start 500 units down. The areas leave room for the blur.
    cloudAreaA = { -120.0f, 440.0f, 2300.0f, 320.0f };
    cloudAreaB = { -100.0f, 440.0f, 2100.0f, 280.0f };
    wispArea   = { 700.0f, 90.0f, 360.0f, 150.0f };

    nextShootingStar = 3.0 + random.nextDouble() * 7.0;
}

void NightSky::advance (double seconds)
{
    now = seconds;
    if (! motion)
    {
        shooting.reset();
        return;
    }

    if (shooting && now - shooting->start > kShootDuration)
        shooting.reset();

    // Rare and random: one every 7 to 24 seconds, from a different spot each time.
    if (! shooting && now >= nextShootingStar)
    {
        ShootingStar s;
        s.start = now;
        s.from = { 120.0f + random.nextFloat() * 520.0f, 30.0f + random.nextFloat() * 110.0f };
        s.length = 100.0f + random.nextFloat() * 60.0f;
        s.angle = juce::degreesToRadians (14.0f + random.nextFloat() * 12.0f);
        shooting = s;
        nextShootingStar = now + 7.0 + random.nextDouble() * 17.0;
    }
}

juce::Image NightSky::renderBlobs (const std::vector<Blob>& blobs, juce::Rectangle<float> area, float pixelsPerUnit, bool ellipses) const
{
    const int w = juce::roundToInt (area.getWidth() * pixelsPerUnit);
    const int h = juce::roundToInt (area.getHeight() * pixelsPerUnit);
    juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);

    // Blobs with different blur amounts are blurred separately, then stacked.
    std::vector<float> blurs;
    for (const auto& b : blobs)
        if (std::find (blurs.begin(), blurs.end(), b.blur) == blurs.end())
            blurs.push_back (b.blur);

    juce::Graphics out (img);
    for (float blur : blurs)
    {
        juce::Image layer (juce::Image::ARGB, img.getWidth(), img.getHeight(), true);
        {
            juce::Graphics g (layer);
            g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (pixelsPerUnit));
            for (const auto& b : blobs)
                if (juce::exactlyEqual (b.blur, blur))
                {
                    g.setColour (b.colour);
                    const juce::Rectangle<float> r (b.x, b.y, b.w, b.h);
                    // CSS border-radius: 50% gives ellipses for clouds; the wisps are pill shapes.
                    if (ellipses)
                        g.fillEllipse (r);
                    else
                        g.fillRoundedRectangle (r, std::min (50.0f, r.getHeight() * 0.5f));
                }
        }
        gaussianBlur (layer, blur * pixelsPerUnit); // CSS filter: blur(N) is a Gaussian with sigma N
        out.drawImageAt (layer, 0, 0);
    }
    return img;
}

void NightSky::rebuildCaches (float pixelScale)
{
    cachedScale = pixelScale;

    // Backdrop: the sky gradient and the moon, at full screen resolution.
    backdrop = juce::Image (juce::Image::ARGB, juce::roundToInt (width * pixelScale), juce::roundToInt (height * pixelScale), true);
    {
        juce::Graphics g (backdrop);
        g.addTransform (juce::AffineTransform::scale (pixelScale));

        // radial-gradient(120% 90% at 78% 8%): an ellipse, drawn as a squashed circle.
        const juce::Point<float> centre (width * 0.78f, height * 0.08f);
        const float rx = width * 1.2f, ry = height * 0.9f;
        {
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, centre.x, centre.y));
            juce::ColourGradient sky (juce::Colour (0xff2a3470), centre, juce::Colour (0xff060918), centre.translated (rx, 0.0f), true);
            sky.addColour (0.38, juce::Colour (0xff151c45));
            sky.addColour (0.72, juce::Colour (0xff0a0f2a));
            g.setGradientFill (sky);
            g.fillRect (juce::Rectangle<float> (-rx, -rx, width + 2.0f * rx, (height + 2.0f * rx) * rx / ry).withCentre (centre));
        }

        // The moon: 150 units across, with a warm halo and a wide, cool one.
        const juce::Rectangle<float> moon (812.0f, 64.0f, 150.0f, 150.0f);
        fillBlurredDisc (g, moon.getCentre(), 75.0f + 40.0f, 120.0f, juce::Colour (160, 170, 230).withAlpha (0.18f));
        fillBlurredDisc (g, moon.getCentre(), 75.0f + 10.0f, 40.0f, juce::Colour (243, 235, 211).withAlpha (0.35f));

        const juce::Point<float> light (moon.getX() + 0.38f * 150.0f, moon.getY() + 0.34f * 150.0f);
        juce::ColourGradient face (juce::Colour (0xfffffaf0), light, juce::Colour (0xffd9cfb3), light.translated (135.8f, 0.0f), true);
        face.addColour (0.45, juce::Colour (0xfff3ebd3));
        g.setGradientFill (face);
        g.fillEllipse (moon);

        struct Crater { float x, y, d, a; };
        for (auto c : { Crater { 38, 42, 26, 0.35f }, Crater { 86, 76, 34, 0.28f },
                        Crater { 60, 104, 16, 0.32f }, Crater { 98, 34, 12, 0.30f } })
        {
            g.setColour (juce::Colour (170, 160, 135).withAlpha (c.a));
            g.fillEllipse (moon.getX() + c.x, moon.getY() + c.y, c.d, c.d);
        }
    }

    // Star sprites: one cell per star size, drawn at twice screen resolution so
    // they stay crisp while they shrink and grow.
    const float spriteScale = pixelScale * 2.0f;
    const int cell = juce::roundToInt (kSpriteCell * spriteScale);
    starSprite = juce::Image (juce::Image::ARGB, cell * 3, cell, true);
    {
        juce::Graphics g (starSprite);
        g.addTransform (juce::AffineTransform::scale (spriteScale));
        for (int i = 0; i < 3; ++i)
        {
            const juce::Point<float> c (kSpriteCell * (static_cast<float> (i) + 0.5f), kSpriteCell * 0.5f);
            const float r = kSpriteSize[i] * 0.5f;
            fillBlurredDisc (g, c, r, kSpriteGlow[i], juce::Colour (255, 244, 214).withAlpha (0.9f));
            g.setColour (juce::Colour (0xfffff8e6));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }
    }
    for (int i = 0; i < 3; ++i)
        starCells[static_cast<size_t> (i)] = starSprite.getClippedImage ({ i * cell, 0, cell, cell });

    // The blurred layers only need building once; they don't depend on screen density.
    if (! cloudImageA.isValid())
    {
        cloudImageA = renderBlobs (cloudsA, cloudAreaA, kCloudResolution, true);
        cloudImageB = renderBlobs (cloudsB, cloudAreaB, kCloudResolution, true);
        wispImage   = renderBlobs (wisps, wispArea, kWispResolution, false);
    }
}

void NightSky::drawStars (juce::Graphics& g) const
{
    const float spriteScale = cachedScale * 2.0f;
    for (const auto& s : stars)
    {
        // CSS twinkle: opacity dips to 20% and the star shrinks to 60%, ease-in-out.
        float dip = 0.0f;
        if (motion)
        {
            const float t = std::fmod (static_cast<float> (now) + s.phase, s.period) / s.period;
            dip = easeInOut (t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f);
        }
        const float opacity = s.opacity * (1.0f - 0.8f * dip);
        const float scale = 1.0f - 0.4f * dip;

        const auto& cellImage = starCells[static_cast<size_t> (spriteIndex (s.size))];
        const float unitsPerPixel = scale / spriteScale;
        const juce::Point<float> centre (s.x + s.size * 0.5f, s.y + s.size * 0.5f);
        g.setOpacity (opacity);
        g.drawImageTransformed (cellImage, juce::AffineTransform::scale (unitsPerPixel)
                                               .translated (centre.x - kSpriteCell * 0.5f * scale,
                                                            centre.y - kSpriteCell * 0.5f * scale));
    }
    g.setOpacity (1.0f);
}

void NightSky::drawShootingStar (juce::Graphics& g, const ShootingStar& s) const
{
    const float u = static_cast<float> ((now - s.start) / kShootDuration);
    if (u < 0.0f || u > 1.0f)
        return;

    const juce::Point<float> dir (std::cos (s.angle), std::sin (s.angle));
    const auto centre = s.from + dir * (kShootTravel * u);
    const auto head = centre + dir * (s.length * 0.5f);
    const auto tail = centre - dir * (s.length * 0.5f);
    const float fade = 1.0f - u;

    const juce::Colour core (0xfffffaf0), glow (255, 250, 235);
    juce::ColourGradient trail (glow.withAlpha (0.0f), tail, glow.withAlpha (0.35f * fade), head, false);
    g.setGradientFill (trail);
    g.drawLine ({ tail, head }, 6.0f);
    trail = juce::ColourGradient (core.withAlpha (0.0f), tail, core.withAlpha (fade), head, false);
    g.setGradientFill (trail);
    g.drawLine ({ tail, head }, 2.0f);
    fillBlurredDisc (g, head, 1.0f, 6.0f, glow.withAlpha (0.9f * fade));
}

void NightSky::draw (juce::Graphics& g)
{
    const float pixelScale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (pixelScale - cachedScale) > 0.01f)
        rebuildCaches (pixelScale);

    g.drawImageTransformed (backdrop, juce::AffineTransform::scale (1.0f / cachedScale));
    drawStars (g);
    if (shooting)
        drawShootingStar (g, *shooting);

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // Wisps drift 110 units and back over 6 s, ease-in-out.
    float wispShift = 0.0f;
    if (motion)
    {
        const double cycle = std::fmod (now, 2.0 * kWispPeriod) / kWispPeriod;
        const float t = static_cast<float> (cycle < 1.0 ? cycle : 2.0 - cycle);
        wispShift = -70.0f + 110.0f * easeInOut (t);
    }
    g.drawImageTransformed (wispImage, juce::AffineTransform::scale (1.0f / kWispResolution)
                                           .translated (wispArea.getX() + wispShift, wispArea.getY()));

    // Two cloud layers roll right at different speeds; each loops after one tile.
    const float u = motion ? static_cast<float> (std::fmod (now, kCloudPeriod) / kCloudPeriod) : 1.0f;
    g.drawImageTransformed (cloudImageB, juce::AffineTransform::scale (1.0f / kCloudResolution)
                                             .translated (cloudAreaB.getX() - kTileB * (1.0f - u), cloudAreaB.getY()));
    g.drawImageTransformed (cloudImageA, juce::AffineTransform::scale (1.0f / kCloudResolution)
                                             .translated (cloudAreaA.getX() - kTileA * (1.0f - u), cloudAreaA.getY()));
}

} // namespace overcast::ui
