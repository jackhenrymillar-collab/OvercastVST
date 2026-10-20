#include "NightSky.h"
#include "Theme.h"

#include <cmath>

namespace overcast::ui
{

namespace
{
    // Draws an alpha-only sprite in the current colour. The clip is narrowed to the
    // sprite first: JUCE's software renderer otherwise builds a mask over the whole
    // clip region for every sprite, which made each frame take a second.
    void drawTinted (juce::Graphics& g, const juce::Image& image, const juce::AffineTransform& t)
    {
        const auto area = image.getBounds().toFloat().transformedBy (t).getSmallestIntegerContainer().expanded (1);
        if (! g.clipRegionIntersects (area))
            return;
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (area);
        g.drawImageTransformed (image, t, true);
    }

    constexpr float kPi = juce::MathConstants<float>::pi;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    // Timings from the approved mockup.
    constexpr double kThemeFade = 3.0;      // themes and the moon's phase change over 3 s
    constexpr double kCloudPeriod = 12.0;   // both cloud layers loop every 12 s
    constexpr float  kTileA = 520.0f, kTileB = 312.0f;
    constexpr double kWispPeriod = 6.0;     // the wisps by the moon drift back and forth
    constexpr double kShootDuration = 0.75;
    constexpr float  kShootTravel = 404.0f;

    // Blurred shapes are cached at a fraction of panel resolution and scaled up.
    constexpr float kLowRes = 0.25f;
    constexpr float kMidRes = 0.5f;

    float easeInOut (float t) { return 0.5f - 0.5f * std::cos (kPi * juce::jlimit (0.0f, 1.0f, t)); }

    // CSS-style looping clock: where an animation of `period` seconds with a
    // (usually negative) `delay` is at time `t`, as 0..1.
    float cycle (double t, float period, float delay)
    {
        if (period <= 0.0f)
            return 0.0f;
        const double u = std::fmod (t - static_cast<double> (delay), static_cast<double> (period)) / period;
        return static_cast<float> (u < 0.0 ? u + 1.0 : u);
    }

    // An "alternate" animation: there and back again, eased each way.
    float alternate (double t, float period, float delay)
    {
        const double u = std::fmod (t - static_cast<double> (delay), 2.0 * period) / period;
        const double w = u < 0.0 ? u + 2.0 : u;
        return easeInOut (static_cast<float> (w < 1.0 ? w : 2.0 - w));
    }

    // The mockup's seeded random numbers (Park-Miller), so the plugin matches it.
    struct Rng
    {
        explicit Rng (std::int64_t s) : seed (s) {}
        float operator()()
        {
            seed = (seed * 16807) % 2147483647;
            return static_cast<float> (static_cast<double> (seed - 1) / 2147483646.0);
        }
        template <typename T, size_t N> T pick (const T (&items)[N]) { return items[static_cast<size_t> ((*this)() * N)]; }
        std::int64_t seed;
    };

    juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour (static_cast<juce::uint8> (r), static_cast<juce::uint8> (g), static_cast<juce::uint8> (b), a); }

    // Fills `area` with an elliptical CSS radial-gradient (radii rx, ry at centre).
    void ellipseGradient (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> centre, float rx, float ry,
                          std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestIntEdges());
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, centre.x, centre.y));
        juce::ColourGradient grad (stops.begin()->second, centre, (stops.end() - 1)->second, centre.translated (rx, 0.0f), true);
        for (const auto& [pos, colour] : stops)
            grad.addColour (juce::jlimit (0.0, 1.0, static_cast<double> (pos)), colour);
        g.setGradientFill (grad);
        const float span = (std::max (area.getWidth(), area.getHeight()) + std::abs (centre.y - area.getCentreY()) * 2.0f) * rx / std::min (rx, ry) + 4000.0f;
        g.fillRect (juce::Rectangle<float> (span, span).withCentre (centre));
    }

    // A vertical CSS linear-gradient over `area`, stops measured from the top.
    void verticalGradient (juce::Graphics& g, juce::Rectangle<float> area, std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        juce::ColourGradient grad (stops.begin()->second, area.getX(), area.getY(),
                                   (stops.end() - 1)->second, area.getX(), area.getBottom(), false);
        for (const auto& [pos, colour] : stops)
            grad.addColour (static_cast<double> (pos), colour);
        g.setGradientFill (grad);
        g.fillRect (area);
    }
}

//==============================================================================
void NightSky::Fade::set (float target, double t, bool instant)
{
    if (instant)
    {
        from = to = target;
        start = t - 1000.0;
        return;
    }
    if (juce::exactlyEqual (target, to))
        return;
    from = at (t);
    to = target;
    start = t;
}

float NightSky::Fade::at (double t) const
{
    const float u = static_cast<float> ((t - start - delay) / length);
    return from + (to - from) * easeInOut (u);
}

float NightSky::phaseFor (int machine) noexcept
{
    // Halo is the full moon. Blossom, Haze and Aurora wax towards it from a crescent;
    // Nebula and Clear Night wane after it. New moon is kept for bypass.
    static constexpr float phases[numMachines] = { 0.17f, 0.28f, 0.39f, 0.5f, 0.66f, 0.82f };
    return phases[juce::jlimit (0, numMachines - 1, machine)];
}

//==============================================================================
NightSky::NightSky()
{
    buildScene();
    nextShootingStar = 3.0 + random.nextDouble() * 7.0;
    for (auto& f : themeFade)
        f.length = kThemeFade;
    // Nebula takes a little longer to bloom, like the mockup.
    themeFade[nebula].length = 4.5;
    themeFade[nebula].delay = 0.75;
}

void NightSky::buildScene()
{
    // The base star field, identical to the first mockup.
    {
        Rng rnd (7);
        constexpr float periods[] = { 2.0f, 3.0f, 4.0f, 6.0f };
        for (int i = 0; i < 90; ++i)
        {
            Dot s {};
            const bool big = rnd() > 0.9f;
            s.x = std::round (rnd() * 1040.0f);
            s.y = std::round (rnd() * 500.0f);
            s.size = big ? 3.0f : (rnd() > 0.5f ? 2.0f : 1.0f);
            s.opacity = 0.35f + rnd() * 0.6f;
            s.glow = big ? 8.0f : 3.0f;
            s.colour = juce::Colour (0xfffff8e6);
            s.period = periods[static_cast<int> (rnd() * 4.0f)];
            s.delay = -rnd() * s.period;
            stars.push_back (s);
        }
    }

    // Nebula: coloured clouds with dark lanes, and tinted dust stars.
    {
        struct B { float x, y, w, h; juce::Colour c; float blur, rot; };
        const B blobs[] = { { 120, 40, 560, 300, rgba (92, 46, 150, 0.42f), 46, -18 },
                            { 300, 90, 460, 220, rgba (176, 70, 160, 0.30f), 40, 12 },
                            { 420, 150, 300, 140, rgba (236, 150, 196, 0.22f), 30, -8 },
                            { 470, 175, 130, 70, rgba (255, 214, 232, 0.30f), 18, 0 },
                            { 600, 60, 380, 260, rgba (52, 120, 170, 0.30f), 44, 24 },
                            { 700, 140, 260, 150, rgba (90, 190, 200, 0.18f), 32, -14 },
                            { 40, 200, 360, 180, rgba (70, 40, 130, 0.30f), 42, 30 },
                            { 330, 210, 420, 60, rgba (4, 4, 14, 0.55f), 16, -10 },
                            { 560, 120, 240, 40, rgba (4, 4, 14, 0.45f), 12, 18 } };
        for (const auto& b : blobs)
            nebulaBlobs.push_back ({ b.x - 40.0f, b.y - 40.0f, b.w, b.h, b.c, b.blur, b.rot }); // the layer sits 40 out

        Rng nr (11);
        const juce::Colour tints[] = { juce::Colour (0xfffff8e6), juce::Colour (0xffe9dcff), juce::Colour (0xffffd9ec), juce::Colour (0xffd6f2ff) };
        constexpr float periods[] = { 3.0f, 4.0f, 6.0f, 8.0f };
        for (int q = 0; q < 70; ++q)
        {
            Dot d {};
            d.period = periods[static_cast<int> (nr() * 4.0f)];
            d.x = std::round (140.0f + nr() * 760.0f);
            d.y = std::round (40.0f + nr() * 330.0f);
            d.size = nr() > 0.85f ? 2.0f : 1.0f;
            d.opacity = 0.3f + nr() * 0.6f;
            d.colour = tints[static_cast<int> (nr() * 4.0f)];
            d.delay = -nr() * d.period;
            d.glow = 0.0f;
            dust.push_back (d);
        }
    }

    // Clear Night's still stars, Blossom's lights and Halo's glints.
    {
        Rng r (23);
        for (int i = 0; i < 80; ++i)
        {
            Dot s {};
            const bool big = r() > 0.85f;
            s.x = std::round (r() * 1040.0f);
            s.y = std::round (r() * 470.0f);
            s.size = big ? 2.5f : r.pick ({ 1.0f, 1.5f, 2.0f });
            s.opacity = 0.5f + r() * 0.5f;
            s.glow = big ? 6.0f : 2.0f;
            s.colour = juce::Colour (0xfffffdf4);
            stillStars.push_back (s);
        }
        for (int i = 0; i < 24; ++i)
        {
            Riser f {};
            f.period = std::round (10.0f + r() * 8.0f);
            f.pulse = 1.8f + r() * 1.8f;
            const bool red = i >= 13;
            f.colour = red ? juce::Colour (0xffff8a72) : juce::Colour (0xffeaff8f);
            f.glow = red ? rgba (255, 90, 70, 0.5f) : rgba (210, 255, 110, 0.5f);
            f.x = std::round (40.0f + r() * 960.0f);
            f.y = std::round (470.0f + r() * 130.0f);
            f.dx = std::round (-40.0f + r() * 80.0f);
            f.size = r() > 0.7f ? 4.0f : 3.0f;
            f.delay = -r() * f.period;
            f.pulseDelay = -r() * f.pulse;
            f.level = i < 12 ? 0.0f : 0.1f + 0.25f * static_cast<float> (i - 12) / 12.0f;
            f.smallGlow = false;
            f.eased = true;
            lights.push_back (f);
        }
        for (float a : { 125.0f, 150.0f, 178.0f, 205.0f, 232.0f, 258.0f })
        {
            const float radius = 150.0f + (r() - 0.5f) * 36.0f, t = juce::degreesToRadians (a);
            Sparkle s {};
            s.period = std::round (4.0f + r() * 3.0f);
            s.x = 75.0f + radius * std::cos (t);
            s.y = 75.0f + radius * std::sin (t);
            s.size = 10.0f;
            s.delay = -r() * s.period;
            s.colour = juce::Colour (0xfff4f8ff);
            moonGlints.push_back (s);
        }
    }

    // Halo: stars and glitter that Shimmer brings in, each at its own level.
    {
        Rng hz (41);
        const juce::Colour ice[] = { juce::Colour (0xffffffff), juce::Colour (0xfff4f8ff), juce::Colour (0xffe2ecff),
                                     juce::Colour (0xfffff4d6), juce::Colour (0xffffe7f2) };
        for (int i = 0; i < 160; ++i)
        {
            Dot s {};
            s.level = (static_cast<float> (i) + 0.5f) / 160.0f;
            s.period = 0.9f + hz() * 1.8f;
            const bool big = hz() > 0.9f;
            s.x = std::round (hz() * 1040.0f);
            s.y = std::round (hz() * 520.0f);
            s.size = big ? 2.5f : (hz() > 0.5f ? 1.5f : 1.0f);
            s.colour = hz.pick (ice);
            s.opacity = 0.45f + hz() * 0.5f;
            s.glow = big ? 7.0f : 3.0f;
            s.delay = -hz() * s.period;
            shimmerStars.push_back (s);
        }
        const juce::Colour glints[] = { juce::Colour (0xffffffff), juce::Colour (0xffffffff), juce::Colour (0xffeaf2ff),
                                        juce::Colour (0xfffff1c8), juce::Colour (0xffffe0f0) };
        for (int i = 0; i < 80; ++i)
        {
            Sparkle s {};
            s.period = 1.1f + hz() * 1.6f;
            s.level = 0.6f + 0.4f * (static_cast<float> (i) + 0.5f) / 80.0f;
            s.x = std::round (hz() * 1030.0f);
            s.y = std::round (hz() * 560.0f);
            s.size = std::round (6.0f + hz() * 10.0f);
            s.colour = hz.pick (glints);
            s.delay = -hz() * s.period;
            glitter.push_back (s);
        }
    }

    // Blossom: more lights and small motes that Feedback brings in.
    {
        Rng bz (53);
        for (int i = 0; i < 32; ++i)
        {
            Riser f {};
            f.period = std::round (10.0f + bz() * 8.0f);
            f.pulse = 1.8f + bz() * 1.8f;
            const bool red = bz() > 0.5f;
            f.level = 0.35f + 0.65f * (static_cast<float> (i) + 0.5f) / 32.0f;
            f.colour = red ? juce::Colour (0xffff8a72) : juce::Colour (0xffeaff8f);
            f.glow = red ? rgba (255, 90, 70, 0.5f) : rgba (210, 255, 110, 0.5f);
            f.x = std::round (40.0f + bz() * 960.0f);
            f.y = std::round (440.0f + bz() * 160.0f);
            f.dx = std::round (-40.0f + bz() * 80.0f);
            f.size = bz() > 0.7f ? 4.0f : 3.0f;
            f.delay = -bz() * f.period;
            f.pulseDelay = -bz() * f.pulse;
            f.smallGlow = false;
            f.eased = true;
            lights.push_back (f);
        }
        for (int i = 0; i < 140; ++i)
        {
            Riser m {};
            m.period = std::round (5.0f + bz() * 6.0f);
            m.pulse = 0.8f + bz() * 1.4f;
            const float k = bz();
            m.level = (static_cast<float> (i) + 0.5f) / 140.0f;
            if (k < 0.45f)      { m.colour = juce::Colour (0xffefffb0); m.glow = rgba (210, 255, 110, 0.45f); }
            else if (k < 0.85f) { m.colour = juce::Colour (0xffffb3a0); m.glow = rgba (255, 100, 80, 0.45f); }
            else                { m.colour = juce::Colour (0xfffff6e0); m.glow = rgba (255, 240, 210, 0.45f); }
            m.x = std::round (bz() * 1040.0f);
            m.y = std::round (380.0f + bz() * 240.0f);
            m.dx = std::round (-30.0f + bz() * 60.0f);
            m.size = bz() > 0.75f ? 2.0f : 1.0f;
            m.delay = -bz() * m.period;
            m.pulseDelay = -bz() * m.pulse;
            m.smallGlow = true;
            m.eased = false;
            motes.push_back (m);
        }
    }

    // Clouds and the wisps by the moon.
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
}

//==============================================================================
void NightSky::setInputs (const Inputs& in)
{
    inputs = in;
    const bool instant = ! started || ! motion;
    started = true;

    // Halo's and Blossom's knobs only count while their machine is on, so their
    // skies keep their look as they fade out.
    if (in.machine == halo)
        shimmerLevel = in.shimmer;
    if (in.machine == blossom)
        feedbackLevel = in.feedback;

    if (in.machine == shownMachine && in.bypass == shownBypass && ! instant)
        return;

    const bool machineChanged = in.machine != shownMachine || in.bypass != shownBypass;
    shownMachine = in.machine;
    shownBypass = in.bypass;

    for (int m = 0; m < numMachines; ++m)
        themeFade[static_cast<size_t> (m)].set (! in.bypass && m == in.machine ? 1.0f : 0.0f, now, instant);

    // Drop the screen-resolution copies of the last machine's layers; the clouds are shared.
    for (auto it = restImages.begin(); it != restImages.end();)
        it = (it->first == &cloudImageA || it->first == &cloudImageB || it->first == &wispImage) ? std::next (it) : restImages.erase (it);

    // Each machine's sky: [moon, clouds, cloud drop, dark, stars, star boost].
    struct Look { float moon, clouds, drop, dark, stars, boost; };
    Look look { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    switch (in.machine)
    {
        case nebula:     look.clouds = 0.35f; look.dark = 0.72f; break;
        case blossom:    look.clouds = 0.0f; look.drop = 60.0f; break;
        case haze:       look.moon = 0.7f; look.stars = 0.65f; break; // the sunrise dims the moon
        case clearNight: look.clouds = 0.0f; look.drop = 60.0f; look.boost = 1.15f; break;
        default: break;
    }
    if (in.bypass)
        look = { 1.0f, 0.0f, 60.0f, 1.0f, 1.0f, 1.0f };

    moonAlpha.set (look.moon, now, instant);
    clouds.set (look.clouds, now, instant);
    cloudDrop.set (look.drop, now, instant);
    dark.set (look.dark, now, instant);
    starAlpha.set (look.stars, now, instant);
    starBoost.set (look.boost, now, instant);
    bypassFade.set (in.bypass ? 1.0f : 0.0f, now, instant);

    // The moon glides to its new phase the shorter way round.
    if (machineChanged || instant)
    {
        const float target = in.bypass ? 0.0f : phaseFor (in.machine);
        float current = target;
        if (! instant)
        {
            const float u = static_cast<float> ((now - phaseStart) / kThemeFade);
            current = phaseFrom + phaseDelta * easeInOut (u);
        }
        phaseFrom = current - std::floor (current);
        float delta = target - phaseFrom;
        delta -= std::round (delta);
        phaseDelta = instant ? 0.0f : delta;
        if (instant)
            phaseFrom = target;
        phaseStart = now;
    }
}

void NightSky::advance (double seconds)
{
    const double dt = std::max (0.0, seconds - lastAdvance);
    lastAdvance = seconds;
    now = seconds;
    if (! inputs.freeze)
        twinkleClock += dt;

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

//==============================================================================
NightSky::Cached NightSky::renderLayer (juce::Rectangle<float> area, float resolution, float blur,
                                        const std::function<void (juce::Graphics&)>& paint) const
{
    Cached c;
    c.area = area;
    c.resolution = resolution;
    c.image = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (area.getWidth() * resolution)),
                           std::max (1, juce::roundToInt (area.getHeight() * resolution)), true);
    {
        juce::Graphics g (c.image);
        g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (resolution));
        paint (g);
    }
    if (blur > 0.0f)
        gaussianBlur (c.image, blur * resolution); // CSS filter: blur(N) is a Gaussian with sigma N
    return c;
}

NightSky::Cached NightSky::renderBlobs (const std::vector<Blob>& blobs, juce::Rectangle<float> area, float resolution, bool ellipses) const
{
    Cached result;
    result.area = area;
    result.resolution = resolution;
    result.image = juce::Image (juce::Image::ARGB, std::max (1, juce::roundToInt (area.getWidth() * resolution)),
                                std::max (1, juce::roundToInt (area.getHeight() * resolution)), true);

    // Blobs with different blur amounts are blurred separately, then stacked.
    std::vector<float> blurs;
    for (const auto& b : blobs)
        if (std::find (blurs.begin(), blurs.end(), b.blur) == blurs.end())
            blurs.push_back (b.blur);

    juce::Graphics out (result.image);
    for (float blur : blurs)
    {
        std::vector<Blob> group;
        for (const auto& b : blobs)
            if (juce::exactlyEqual (b.blur, blur))
                group.push_back (b);
        auto layer = renderLayer (area, resolution, blur, [&] (juce::Graphics& g)
        {
            for (const auto& b : group)
            {
                g.setColour (b.colour);
                const juce::Rectangle<float> r (b.x, b.y, b.w, b.h);
                juce::Path p;
                // CSS border-radius: 50% gives ellipses for clouds; the wisps are pill shapes.
                if (ellipses)
                    p.addEllipse (r);
                else
                    p.addRoundedRectangle (r, std::min (50.0f, r.getHeight() * 0.5f));
                g.fillPath (p, juce::AffineTransform::rotation (juce::degreesToRadians (b.rotation), r.getCentreX(), r.getCentreY()));
            }
        });
        out.drawImageAt (layer.image, 0, 0);
    }
    return result;
}

void NightSky::drawCached (juce::Graphics& g, const Cached& c, float opacity, juce::AffineTransform extra) const
{
    if (opacity <= 0.002f || ! c.image.isValid())
        return;
    g.setOpacity (juce::jmin (1.0f, opacity));
    g.drawImageTransformed (c.image, juce::AffineTransform::scale (1.0f / c.resolution)
                                         .translated (c.area.getX(), c.area.getY())
                                         .followedBy (extra));
    g.setOpacity (1.0f);
}

void NightSky::drawAtRest (juce::Graphics& g, const Cached& c, float opacity, juce::Point<float> offset)
{
    if (opacity <= 0.002f || ! c.image.isValid())
        return;

    auto it = restImages.find (&c);
    if (it == restImages.end())
    {
        const auto size = (c.area * cachedScale).getSmallestIntegerContainer();
        juce::Image img (juce::Image::ARGB, std::max (1, size.getWidth()), std::max (1, size.getHeight()), true);
        {
            juce::Graphics rg (img);
            rg.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            rg.addTransform (juce::AffineTransform::translation (-c.area.getX(), -c.area.getY()).scaled (cachedScale));
            drawCached (rg, c, 1.0f);
        }
        it = restImages.emplace (&c, std::move (img)).first;
    }

    // Snap to whole pixels so the copy is blitted rather than resampled.
    const float x = std::round ((c.area.getX() + offset.x) * cachedScale) / cachedScale;
    const float y = std::round ((c.area.getY() + offset.y) * cachedScale) / cachedScale;
    g.setOpacity (juce::jmin (1.0f, opacity));
    g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / cachedScale).translated (x, y));
    g.setOpacity (1.0f);
}

//==============================================================================
const juce::Image& NightSky::dotSprite (float size, float glow)
{
    // One alpha-only sprite per size and glow, tinted when drawn.
    const int key = juce::roundToInt (size * 4.0f) * 1000 + juce::roundToInt (glow * 4.0f);
    auto it = sprites.find (key);
    if (it != sprites.end())
        return it->second;

    const float scale = cachedScale * 2.0f; // twice screen resolution so they stay crisp as they twinkle
    const float cellUnits = size + glow * 3.0f + 4.0f;
    const int cell = std::max (2, juce::roundToInt (cellUnits * scale));
    juce::Image img (juce::Image::SingleChannel, cell, cell, true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        const juce::Point<float> c (cellUnits * 0.5f, cellUnits * 0.5f);
        const float r = size * 0.5f;
        if (glow > 0.0f)
            fillBlurredDisc (g, c, r, glow, juce::Colours::white.withAlpha (0.9f));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));
    }
    return sprites.emplace (key, std::move (img)).first->second;
}

void NightSky::drawDot (juce::Graphics& g, const Dot& d, float alpha, double clock)
{
    // CSS twinkle: opacity dips to 20% and the star shrinks to 60%, ease-in-out.
    float dip = 0.0f;
    if (motion && d.period > 0.0f)
    {
        const float t = cycle (clock, d.period, d.delay);
        dip = easeInOut (t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f);
    }
    const float opacity = juce::jmin (1.0f, d.opacity * alpha * (1.0f - 0.8f * dip));
    if (opacity <= 0.003f)
        return;
    const float scale = 1.0f - 0.4f * dip;

    const auto& sprite = dotSprite (d.size, d.glow);
    const float unitsPerPixel = scale / (cachedScale * 2.0f);
    const float half = static_cast<float> (sprite.getWidth()) * 0.5f * unitsPerPixel;
    const juce::Point<float> centre (d.x + d.size * 0.5f, d.y + d.size * 0.5f);
    g.setColour (d.colour.withAlpha (opacity));
    drawTinted (g, sprite, juce::AffineTransform::scale (unitsPerPixel).translated (centre.x - half, centre.y - half));
}

void NightSky::drawDots (juce::Graphics& g, const std::vector<Dot>& dots, float alpha, double clock, float knob)
{
    if (alpha <= 0.002f)
        return;
    for (const auto& d : dots)
    {
        // Knob-driven dots fade in over a short stretch of the knob's travel.
        const float show = juce::jlimit (0.0f, 1.0f, (knob - d.level) * 25.0f + 1.0f);
        if (show > 0.0f)
            drawDot (g, d, alpha * show, clock);
    }
}

void NightSky::drawSparkle (juce::Graphics& g, juce::Point<float> centre, float size, float u, juce::Colour colour, float alpha) const
{
    // CSS glint: hidden for 70% of the cycle, then flares, turning 45 degrees, and fades.
    float k = 0.0f;
    if (u > 0.7f)
        k = u < 0.8f ? easeInOut ((u - 0.7f) / 0.1f) : 1.0f - easeInOut ((u - 0.8f) / 0.2f);
    if (k <= 0.01f || alpha <= 0.002f)
        return;
    const float s = size * (0.3f + 0.7f * k) / 10.0f; // the sprite is 10 units across
    const float spriteUnits = 10.0f / static_cast<float> (sparkleSprite.getWidth());
    g.setColour (colour.withAlpha (juce::jmin (1.0f, alpha * k)));
    drawTinted (g, sparkleSprite, juce::AffineTransform::scale (spriteUnits)
                                               .translated (-5.0f, -5.0f)
                                               .scaled (s)
                                               .rotated (kPi * 0.25f * k)
                                               .translated (centre.x, centre.y));
}

void NightSky::drawRiser (juce::Graphics& g, const Riser& f, float alpha)
{
    // CSS flyup: up 140 and across, then up 300 and back; fading in and out at the ends.
    const float u = cycle (twinkleClock, f.period, f.delay);
    const auto seg = [&f] (float t) { return f.eased ? easeInOut (t) : t; };
    float x, y;
    if (u < 0.5f) { const float t = seg (u / 0.5f); x = f.dx * t; y = -140.0f * t; }
    else          { const float t = seg ((u - 0.5f) / 0.5f); x = f.dx + (-1.6f * f.dx) * t; y = -140.0f - 160.0f * t; }
    float fade = 1.0f;
    if (u < 0.15f) fade = seg (u / 0.15f);
    else if (u > 0.85f) fade = 1.0f - seg ((u - 0.85f) / 0.15f);
    const float p = cycle (twinkleClock, f.pulse, f.pulseDelay);
    const float pulse = 0.15f + 0.85f * easeInOut (p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f);

    const float a = alpha * fade * pulse;
    if (a <= 0.003f)
        return;
    const juce::Point<float> c (f.x + x + f.size * 0.5f, f.y + y + f.size * 0.5f);
    const float glowRadius = f.smallGlow ? 1.0f + f.size * 0.5f : 3.0f + f.size * 0.5f;
    const float glowBlur = f.smallGlow ? 3.0f : 8.0f;
    const auto& glowSprite = dotSprite (glowRadius * 2.0f, glowBlur);
    const float upp = 1.0f / (cachedScale * 2.0f);
    const float half = static_cast<float> (glowSprite.getWidth()) * 0.5f * upp;
    g.setColour (f.glow.withMultipliedAlpha (a));
    drawTinted (g, glowSprite, juce::AffineTransform::scale (upp).translated (c.x - half, c.y - half));
    g.setColour (f.colour.withAlpha (a));
    g.fillEllipse (juce::Rectangle<float> (f.size, f.size).withCentre (c));
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

//==============================================================================
void NightSky::rebuildMoon (int toneStep)
{
    moonTone = toneStep;
    const float tone = static_cast<float> (toneStep) / 50.0f;

    // Tone tints the moon: blood red at minimum, warm cream in the centre, white at maximum.
    struct Palette { juce::Colour c0, c1, c2, crater, glow, halo; };
    const Palette blood { juce::Colour (236, 140, 96), juce::Colour (190, 70, 44), juce::Colour (112, 32, 22),
                          juce::Colour (90, 24, 14), juce::Colour (214, 72, 48), juce::Colour (150, 50, 60) };
    const Palette cream { juce::Colour (255, 250, 240), juce::Colour (243, 235, 211), juce::Colour (217, 207, 179),
                          juce::Colour (170, 160, 135), juce::Colour (243, 235, 211), juce::Colour (160, 170, 230) };
    const Palette white { juce::Colour (255, 255, 255), juce::Colour (242, 245, 252), juce::Colour (214, 220, 233),
                          juce::Colour (150, 158, 178), juce::Colour (236, 242, 255), juce::Colour (170, 190, 255) };
    const auto& towards = tone < 0.0f ? blood : white;
    const float m = std::abs (tone);
    const Palette p { cream.c0.interpolatedWith (towards.c0, m), cream.c1.interpolatedWith (towards.c1, m),
                      cream.c2.interpolatedWith (towards.c2, m), cream.crater.interpolatedWith (towards.crater, m),
                      cream.glow.interpolatedWith (towards.glow, m), cream.halo.interpolatedWith (towards.halo, m) };

    const auto moon = moonBounds();
    const float pad = 200.0f;
    const auto area = moon.expanded (pad);
    const float scale = cachedScale;

    moonGlow = juce::Image (juce::Image::ARGB, juce::roundToInt (area.getWidth() * scale), juce::roundToInt (area.getHeight() * scale), true);
    {
        juce::Graphics g (moonGlow);
        g.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));
        fillBlurredDisc (g, moon.getCentre(), 75.0f + 40.0f, 120.0f, p.halo.withAlpha (0.18f));
        fillBlurredDisc (g, moon.getCentre(), 75.0f + 10.0f, 40.0f, p.glow.withAlpha (0.35f));
    }

    moonDisc = juce::Image (juce::Image::ARGB, juce::roundToInt (moon.getWidth() * scale) + 2, juce::roundToInt (moon.getHeight() * scale) + 2, true);
    {
        juce::Graphics g (moonDisc);
        g.addTransform (juce::AffineTransform::translation (-moon.getX() + 1.0f / scale, -moon.getY() + 1.0f / scale).scaled (scale));
        const juce::Point<float> light (moon.getX() + 0.38f * 150.0f, moon.getY() + 0.34f * 150.0f);
        juce::ColourGradient face (p.c0, light, p.c2, light.translated (135.8f, 0.0f), true);
        face.addColour (0.45, p.c1);
        g.setGradientFill (face);
        g.fillEllipse (moon);

        struct Crater { float x, y, d, a; };
        for (auto c : { Crater { 38, 42, 26, 0.35f }, Crater { 86, 76, 34, 0.28f },
                        Crater { 60, 104, 16, 0.32f }, Crater { 98, 34, 12, 0.30f } })
        {
            g.setColour (p.crater.withAlpha (c.a));
            g.fillEllipse (moon.getX() + c.x, moon.getY() + c.y, c.d, c.d);
        }
    }
}

void NightSky::drawMoon (juce::Graphics& g, float alpha)
{
    const int toneStep = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, inputs.tone) * 50.0f);
    if (toneStep != moonTone || ! moonDisc.isValid())
        rebuildMoon (toneStep);

    const auto moon = moonBounds();
    const auto centre = moon.getCentre();

    // Where the moon is in its cycle right now.
    const float u = static_cast<float> ((now - phaseStart) / kThemeFade);
    float phase = phaseFrom + phaseDelta * (motion ? easeInOut (u) : 1.0f);
    phase -= std::floor (phase);
    const float lit = 0.5f - 0.5f * std::cos (kTwoPi * phase);

    // Halo's coloured ring, growing into place as the theme fades in.
    const float ringOn = themeAlpha (halo);
    if (ringOn > 0.002f)
    {
        const float s = 0.85f + 0.15f * ringOn;
        const float r = 155.0f * s;
        juce::ColourGradient ring (rgba (255, 240, 230, 0.0f), centre, rgba (170, 190, 255, 0.0f), centre.translated (r, 0.0f), true);
        ring.addColour (0.78, rgba (255, 240, 230, 0.0f));
        ring.addColour (0.84, rgba (255, 170, 140, 0.18f));
        ring.addColour (0.88, rgba (255, 246, 228, 0.30f));
        ring.addColour (0.92, rgba (190, 210, 255, 0.22f));
        ring.addColour (0.96, rgba (170, 190, 255, 0.08f));
        g.setOpacity (alpha * ringOn);
        g.setGradientFill (ring);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
        g.setOpacity (1.0f);
    }

    // The glow follows how much of the moon is lit.
    const auto glowArea = moon.expanded (200.0f);
    g.setOpacity (alpha * (0.08f + 0.92f * lit));
    g.drawImageTransformed (moonGlow, juce::AffineTransform::scale (1.0f / cachedScale).translated (glowArea.getX(), glowArea.getY()));
    g.setOpacity (alpha);
    g.drawImageTransformed (moonDisc, juce::AffineTransform::scale (1.0f / cachedScale).translated (moon.getX() - 1.0f / cachedScale,
                                                                                                    moon.getY() - 1.0f / cachedScale));
    g.setOpacity (1.0f);

    // The shadow: from the dark limb to the terminator, an ellipse whose width
    // follows the phase. Waxing moons are dark on the left, waning on the right.
    if (std::abs (phase - 0.5f) > 0.004f)
    {
        const float k = std::cos (kTwoPi * phase);
        const bool waxing = phase < 0.5f;
        constexpr int steps = 48;
        constexpr float R = 75.0f;
        juce::Path shadow;
        for (int i = 0; i <= steps; ++i)
        {
            const float s = -1.0f + 2.0f * static_cast<float> (i) / steps;
            const float w = R * std::sqrt (std::max (0.0f, 1.0f - s * s));
            const float x = waxing ? -w : w;
            const juce::Point<float> pt (centre.x + x, centre.y + R * s);
            if (i == 0) shadow.startNewSubPath (pt); else shadow.lineTo (pt);
        }
        for (int i = steps; i >= 0; --i)
        {
            const float s = -1.0f + 2.0f * static_cast<float> (i) / steps;
            const float w = R * std::sqrt (std::max (0.0f, 1.0f - s * s));
            const float x = waxing ? k * w : -k * w;
            shadow.lineTo (centre.x + x, centre.y + R * s);
        }
        shadow.closeSubPath();
        g.setColour (juce::Colour (0xff0c1130).withAlpha (0.9f * alpha));
        g.fillPath (shadow);
    }

    // A faint rim, so the new moon of bypass can still be found.
    const float rim = bypassFade.at (now);
    if (rim > 0.002f)
    {
        g.setColour (rgba (200, 212, 255, 0.32f * rim * alpha));
        g.drawEllipse (moon.reduced (0.5f), 1.0f);
    }

    // Halo's glints around the moon.
    if (ringOn > 0.002f)
        for (const auto& s : moonGlints)
            drawSparkle (g, { moon.getX() + s.x, moon.getY() + s.y }, s.size, cycle (twinkleClock, s.period, s.delay),
                         s.colour, alpha * ringOn);
}

//==============================================================================
void NightSky::rebuildCaches (float pixelScale)
{
    cachedScale = pixelScale;
    sprites.clear();
    restImages.clear();
    baseImage = {};
    baseKey = -1.0f;
    moonTone = 1000;

    const juce::Rectangle<float> panel (0.0f, 0.0f, width, height);

    // Backdrop: the sky gradient, at full screen resolution.
    backdrop = juce::Image (juce::Image::ARGB, juce::roundToInt (width * pixelScale), juce::roundToInt (height * pixelScale), true);
    {
        juce::Graphics g (backdrop);
        g.addTransform (juce::AffineTransform::scale (pixelScale));
        ellipseGradient (g, panel, { width * 0.78f, height * 0.08f }, width * 1.2f, height * 0.9f,
                         { { 0.0f, juce::Colour (0xff2a3470) }, { 0.38f, juce::Colour (0xff151c45) },
                           { 0.72f, juce::Colour (0xff0a0f2a) }, { 1.0f, juce::Colour (0xff060918) } });
    }

    // Bright sparkle sprite: a soft centre and a thin cross, 10 units across.
    {
        const int px = std::max (8, juce::roundToInt (10.0f * pixelScale * 2.0f));
        sparkleSprite = juce::Image (juce::Image::SingleChannel, px, px, true);
        juce::Graphics g (sparkleSprite);
        g.addTransform (juce::AffineTransform::scale (static_cast<float> (px) / 10.0f));
        fillBlurredDisc (g, { 5.0f, 5.0f }, 0.6f, 2.5f, juce::Colours::white);
        for (bool across : { true, false })
        {
            juce::ColourGradient line (juce::Colours::transparentWhite, across ? 0.0f : 5.0f, across ? 5.0f : 0.0f,
                                       juce::Colours::transparentWhite, across ? 10.0f : 5.0f, across ? 5.0f : 10.0f, false);
            line.addColour (0.5, juce::Colours::white);
            g.setGradientFill (line);
            g.fillRect (across ? juce::Rectangle<float> (0.0f, 4.6f, 10.0f, 0.8f) : juce::Rectangle<float> (4.6f, 0.0f, 0.8f, 10.0f));
        }
    }

    // Everything below is drawn at a fixed resolution, so only build it once.
    if (cloudImageA.image.isValid())
        return;

    cloudImageA = renderBlobs (cloudsA, { -120.0f, 440.0f, 2300.0f, 320.0f }, kLowRes, true);
    cloudImageB = renderBlobs (cloudsB, { -100.0f, 440.0f, 2100.0f, 280.0f }, kLowRes, true);
    wispImage   = renderBlobs (wisps, { 700.0f, 90.0f, 360.0f, 150.0f }, kMidRes, false);

    // Pitch-black sky for Nebula and bypass.
    darkSky = renderLayer (panel, kLowRes, 0.0f, [] (juce::Graphics& g)
    {
        ellipseGradient (g, { 0.0f, 0.0f, width, height }, { width * 0.5f, height * 0.3f }, width * 1.3f, height * 1.0f,
                         { { 0.0f, juce::Colour (0xff03040d) }, { 0.6f, juce::Colour (0xff020309) }, { 1.0f, juce::Colour (0xff010106) } });
    });

    // Nebula: one image per cloud, so each can glow in its own time.
    // (Three groups rather than one image per cloud: far cheaper to draw, and still uneven.)
    for (size_t group = 0; group < 3; ++group)
    {
        std::vector<Blob> members;
        juce::Rectangle<float> area;
        for (size_t i = group; i < nebulaBlobs.size(); i += 3)
        {
            const auto& b = nebulaBlobs[i];
            const float pad = b.blur * 3.0f + std::max (b.w, b.h) * 0.3f;
            const juce::Rectangle<float> r (b.x - pad, b.y - pad, b.w + pad * 2.0f, b.h + pad * 2.0f);
            area = members.empty() ? r : area.getUnion (r);
            members.push_back (b);
        }
        nebulaImages.push_back (renderBlobs (members, area, kLowRes, true));
    }

    // Aurora: six curtains of green and violet light.
    {
        const juce::String greenTop = "green";
        struct Curtain { float x, w, h; bool violet; };
        for (auto c : { Curtain { 30, 190, 240, false }, Curtain { 160, 230, 280, true }, Curtain { 300, 200, 260, false },
                        Curtain { 430, 240, 290, false }, Curtain { 560, 180, 230, true }, Curtain { 650, 150, 210, false } })
        {
            const juce::Rectangle<float> r (c.x, -20.0f, c.w, c.h);
            auto img = renderLayer (r.expanded (48.0f), kLowRes, 16.0f, [r, c] (juce::Graphics& g)
            {
                juce::Path p;
                p.addRoundedRectangle (r, r.getWidth() * 0.4f);
                juce::ColourGradient grad (juce::Colours::transparentBlack, 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getBottom(), false);
                if (c.violet)
                {
                    grad = juce::ColourGradient (rgba (200, 110, 255, 0.0f), 0.0f, r.getY(), rgba (110, 220, 200, 0.0f), 0.0f, r.getBottom(), false);
                    grad.addColour (0.35, rgba (190, 100, 255, 0.26f));
                    grad.addColour (0.70, rgba (110, 220, 200, 0.22f));
                }
                else
                {
                    grad = juce::ColourGradient (rgba (150, 100, 255, 0.0f), 0.0f, r.getY(), rgba (70, 240, 170, 0.0f), 0.0f, r.getBottom(), false);
                    grad.addColour (0.25, rgba (150, 100, 255, 0.22f));
                    grad.addColour (0.62, rgba (70, 240, 170, 0.34f));
                }
                g.setGradientFill (grad);
                g.fillPath (p);
            });
            auroraImages.push_back (img);
        }
    }

    // Haze: a sunrise glowing up from below the horizon, through the mist.
    sunrise = renderLayer (panel, kMidRes, 0.0f, [] (juce::Graphics& g)
    {
        verticalGradient (g, { 0.0f, 0.0f, width, height }, { { 0.0f, rgba (255, 140, 90, 0.0f) }, { 0.25f, rgba (255, 140, 90, 0.0f) },
                                                             { 0.6f, rgba (255, 140, 90, 0.07f) }, { 1.0f, rgba (255, 160, 90, 0.16f) } });
        ellipseGradient (g, { 0.0f, 200.0f, width, 420.0f }, { width * 0.5f, 200.0f + 420.0f * 1.12f }, width * 0.45f, 420.0f * 0.75f,
                         { { 0.0f, rgba (255, 214, 130, 0.6f) }, { 0.18f, rgba (255, 170, 80, 0.4f) }, { 0.4f, rgba (245, 120, 60, 0.18f) },
                           { 0.65f, rgba (220, 100, 80, 0.06f) }, { 0.85f, rgba (220, 100, 80, 0.0f) }, { 1.0f, rgba (220, 100, 80, 0.0f) } });
        ellipseGradient (g, { 0.0f, 360.0f, width, 260.0f }, { width * 0.5f, 360.0f + 260.0f * 1.18f }, width * 0.28f, 260.0f * 0.55f,
                         { { 0.0f, rgba (255, 236, 170, 0.55f) }, { 0.45f, rgba (255, 196, 110, 0.2f) }, { 0.8f, rgba (255, 196, 110, 0.0f) }, { 1.0f, rgba (255, 196, 110, 0.0f) } });
        verticalGradient (g, { 0.0f, 480.0f, width, 140.0f }, { { 0.0f, rgba (255, 170, 110, 0.0f) }, { 0.4f, rgba (255, 170, 110, 0.07f) },
                                                               { 1.0f, rgba (255, 150, 90, 0.12f) } });
    });

    // Haze: a pale wash and slow drifting bands of mist.
    hazeTint = renderLayer (panel, kLowRes, 0.0f, [] (juce::Graphics& g)
    {
        verticalGradient (g, { 0.0f, 0.0f, width, height }, { { 0.0f, rgba (190, 200, 230, 0.04f) }, { 1.0f, rgba (190, 200, 230, 0.12f) } });
    });
    struct Mist { float y, h, a, blur; };
    for (auto m : { Mist { 40, 140, 0.1f, 34 }, Mist { 170, 160, 0.13f, 36 }, Mist { 330, 150, 0.07f, 40 }, Mist { 470, 180, 0.16f, 36 } })
    {
        const juce::Rectangle<float> r (-180.0f, m.y, 1400.0f, m.h);
        mistImages.push_back (renderLayer (r.expanded (m.blur * 3.0f), kLowRes, m.blur, [r, m] (juce::Graphics& g)
        {
            ellipseGradient (g, r, r.getCentre(), r.getWidth() * 0.5f, r.getHeight() * 0.5f,
                             { { 0.0f, rgba (205, 214, 238, m.a) }, { 0.7f, rgba (205, 214, 238, 0.0f) }, { 1.0f, rgba (205, 214, 238, 0.0f) } });
        }));
    }
}

//==============================================================================
void NightSky::drawBase (juce::Graphics& g)
{
    // The gradient and the darkening for Nebula and bypass.
    const float key = dark.at (now);
    auto paintLayers = [this, key] (juce::Graphics& lg)
    {
        lg.drawImageTransformed (backdrop, juce::AffineTransform::scale (1.0f / cachedScale));
        drawCached (lg, darkSky, key);
    };

    // While the darkening is fading, draw it as it is; once still, flatten it into one image.
    if (! dark.settled (now))
    {
        paintLayers (g);
        return;
    }
    if (! juce::exactlyEqual (key, baseKey) || ! baseImage.isValid())
    {
        baseImage = juce::Image (juce::Image::ARGB, backdrop.getWidth(), backdrop.getHeight(), false);
        juce::Graphics bg (baseImage);
        bg.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        bg.addTransform (juce::AffineTransform::scale (cachedScale));
        paintLayers (bg);
        baseKey = key;
    }
    g.drawImageTransformed (baseImage, juce::AffineTransform::scale (1.0f / cachedScale));
}

void NightSky::draw (juce::Graphics& g)
{
    const float pixelScale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::abs (pixelScale - cachedScale) > 0.01f)
        rebuildCaches (pixelScale);

    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);

    const double t = motion ? now : 0.0;          // slow drifts
    const double tw = twinkleClock;               // twinkles and risers, held by Freeze
    auto on = [this] (int m) { return themeAlpha (m); };

    drawBase (g);

    // Nebula drifts and breathes; its dust twinkles in colour.
    if (const float a = on (nebula); a > 0.002f)
    {
        const float d = motion ? alternate (t, 38.0f, 0.0f) : 0.5f;
        // It drifts by sliding only, so once it has faded in it can be blitted.
        const juce::Point<float> drift (-18.0f + 40.0f * d, 6.0f - 14.0f * d);
        const bool rest = atRest (nebula);
        for (size_t i = 0; i < nebulaImages.size(); ++i)
        {
            const float p = 6.0f + static_cast<float> (i) * 3.0f;
            const float glow = motion ? 0.75f + 0.25f * (1.0f - std::abs (2.0f * cycle (t, 2.0f * p, -3.0f * static_cast<float> (i)) - 1.0f)) : 1.0f;
            rest ? drawAtRest (g, nebulaImages[i], a * glow, drift)
                 : drawCached (g, nebulaImages[i], a * glow, juce::AffineTransform::translation (drift));
        }
        drawDots (g, dust, a, tw);
    }

    // The ordinary stars, dimmed or brightened by the theme.
    drawDots (g, stars, starAlpha.at (now) * starBoost.at (now), tw);
    drawDots (g, stillStars, on (clearNight), tw);

    // Halo: Shimmer fills the sky with quick stars, then glitter near the top of its range.
    if (const float a = on (halo); a > 0.002f)
    {
        drawDots (g, shimmerStars, a, tw, shimmerLevel);
        for (const auto& s : glitter)
        {
            const float show = juce::jlimit (0.0f, 1.0f, (shimmerLevel - s.level) * 25.0f + 1.0f);
            if (show > 0.0f)
                drawSparkle (g, { s.x + s.size * 0.5f, s.y + s.size * 0.5f }, s.size, cycle (tw, s.period, s.delay), s.colour, a * show);
        }
    }

    // Aurora's curtains sway and pulse, hinged at the top.
    if (const float a = on (aurora); a > 0.002f)
    {
        constexpr float xs[] = { 30, 160, 300, 430, 560, 650 }, ws[] = { 190, 230, 200, 240, 180, 150 };
        for (size_t i = 0; i < auroraImages.size(); ++i)
        {
            const float fi = static_cast<float> (i);
            const float s = motion ? alternate (t, 9.0f + fi * 1.7f, -fi * 2.3f) : 0.5f;
            const float p = 6.0f + static_cast<float> (i % 3) * 2.0f;
            const float pulse = motion ? 0.75f + 0.25f * (1.0f - std::abs (2.0f * cycle (t, p, -fi * 2.3f) - 1.0f)) : 1.0f;
            const juce::Point<float> hinge (xs[i] + ws[i] * 0.5f, -20.0f);
            const float skew = std::tan (juce::degreesToRadians (-14.0f + 26.0f * s));
            const auto sway = juce::AffineTransform::translation (-hinge.x, -hinge.y)
                                  .scaled (1.0f, 0.94f + 0.14f * s)
                                  .followedBy (juce::AffineTransform::shear (skew, 0.0f))
                                  .translated (hinge.x - 30.0f + 60.0f * s, hinge.y);
            drawCached (g, auroraImages[i], a * pulse, sway);
        }
    }

    if (shooting)
        drawShootingStar (g, *shooting);

    drawMoon (g, moonAlpha.at (now));

    // Clouds: wisps by the moon and two layers rolling along the bottom.
    if (const float a = clouds.at (now); a > 0.002f)
    {
        const float drop = cloudDrop.at (now);
        float wispShift = 0.0f;
        if (motion)
            wispShift = -70.0f + 110.0f * alternate (now, static_cast<float> (kWispPeriod), 0.0f);
        const float u = motion ? static_cast<float> (std::fmod (now, kCloudPeriod) / kCloudPeriod) : 1.0f;
        if (clouds.settled (now) && cloudDrop.settled (now))
        {
            drawAtRest (g, wispImage, a, { wispShift, drop });
            drawAtRest (g, cloudImageB, a, { -kTileB * (1.0f - u), drop });
            drawAtRest (g, cloudImageA, a, { -kTileA * (1.0f - u), drop });
        }
        else
        {
            drawCached (g, wispImage, a, juce::AffineTransform::translation (wispShift, drop));
            drawCached (g, cloudImageB, a, juce::AffineTransform::translation (-kTileB * (1.0f - u), drop));
            drawCached (g, cloudImageA, a, juce::AffineTransform::translation (-kTileA * (1.0f - u), drop));
        }
    }

    // Haze: the sun's glow comes up from below, and mist bands drift slowly to and fro.
    if (const float a = on (haze); a > 0.002f)
    {
        const bool rest = atRest (haze);
        rest ? drawAtRest (g, sunrise, a) : drawCached (g, sunrise, a, juce::AffineTransform::translation (0.0f, 120.0f * (1.0f - a)));
        rest ? drawAtRest (g, hazeTint, a) : drawCached (g, hazeTint, a);
        constexpr float periods[] = { 46, 38, 52, 34 }, delays[] = { -10, -20, -5, -14 };
        for (size_t i = 0; i < mistImages.size(); ++i)
        {
            const float s = motion ? alternate (t, periods[i], delays[i]) : 0.5f;
            const float x = -90.0f + 180.0f * s;
            rest ? drawAtRest (g, mistImages[i], a, { x, 0.0f })
                 : drawCached (g, mistImages[i], a, juce::AffineTransform::translation (x, 0.0f));
        }
    }

    // Blossom: lights and motes rise; Feedback decides how many.
    if (const float a = on (blossom); a > 0.002f)
    {
        for (const auto& m : motes)
            if (const float show = juce::jlimit (0.0f, 1.0f, (feedbackLevel - m.level) * 25.0f + 1.0f); show > 0.0f)
                drawRiser (g, m, a * show);
        for (const auto& f : lights)
            if (const float show = juce::jlimit (0.0f, 1.0f, (feedbackLevel - f.level) * 25.0f + 1.0f); show > 0.0f)
                drawRiser (g, f, a * show);
    }
}

} // namespace overcast::ui
