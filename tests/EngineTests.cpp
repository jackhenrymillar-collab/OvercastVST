#include "TestHelpers.h"
#include "dsp/ReverbEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace overcast::dsp;
using namespace overcast::test;

namespace
{
    std::vector<float> noise (std::size_t n, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-0.8f, 0.8f);
        std::vector<float> v (n);
        for (auto& x : v) x = d (rng);
        return v;
    }
}

TEST_CASE ("Dry path is bit-exact at 0% mix, for any block size")
{
    for (int block : { 1, 7, 64, 513, 4096 })
    {
        ReverbEngine engine;
        EngineParams p;
        p.mix = 0.0f;
        p.mixLaw = MixLaw::equalPower;
        engine.setParams (p);
        engine.prepare (48000.0, block);

        const auto inL = noise (48000, 1), inR = noise (48000, 2);
        auto l = inL, r = inR;
        for (std::size_t i = 0; i < l.size(); i += static_cast<std::size_t> (block))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), l.size() - i));
            engine.process (l.data() + i, r.data() + i, n);
        }
        INFO ("block size " << block);
        CHECK (l == inL);
        CHECK (r == inR);
    }
}

TEST_CASE ("Engine handles blocks larger than prepared")
{
    ReverbEngine engine;
    engine.prepare (48000.0, 128);
    auto l = noise (5000, 3), r = noise (5000, 4);
    engine.process (l.data(), r.data(), 5000);
    CHECK (allFinite (l));
    CHECK (allFinite (r));
}

TEST_CASE ("Silence in gives silence out, with no NaN or denormal build-up")
{
    ReverbEngine engine;
    EngineParams p;
    p.mix = 1.0f;
    p.machineParams.decaySeconds = 30.0f;
    engine.setParams (p);
    engine.prepare (48000.0, 256);
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    for (int i = 0; i < 2000; ++i)
        engine.process (l.data(), r.data(), 256);
    CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }));
}

TEST_CASE ("Wet output never exceeds full scale, even with hot input and freeze")
{
    ReverbEngine engine;
    EngineParams p;
    p.mix = 1.0f;
    p.killDry = true;
    p.machineParams.decaySeconds = 60.0f;
    p.machineParams.freeze = false;
    engine.setParams (p);
    engine.prepare (48000.0, 512);

    float peak = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 100) { p.machineParams.freeze = true; engine.setParams (p); }
        std::vector<float> l (512), r (512);
        if (b < 100)
        {
            l = noise (512, static_cast<unsigned> (b));
            for (auto& v : l) v *= 4.0f; // +12 dB over full scale
            r = l;
        }
        engine.process (l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i)
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
    }
    CHECK (peak <= 1.0f);
}

TEST_CASE ("Mix laws")
{
    float d = 0, w = 0;
    ReverbEngine::mixGains (0.5f, MixLaw::equalPower, false, d, w);
    CHECK (d == Catch::Approx (std::sqrt (0.5f)));
    CHECK (w == Catch::Approx (std::sqrt (0.5f)));

    ReverbEngine::mixGains (0.25f, MixLaw::pedal, false, d, w);
    CHECK (d == 1.0f);
    CHECK (w == Catch::Approx (0.5f));
    ReverbEngine::mixGains (0.75f, MixLaw::pedal, false, d, w);
    CHECK (d == Catch::Approx (0.5f));
    CHECK (w == 1.0f);

    ReverbEngine::mixGains (0.3f, MixLaw::pedal, true, d, w);
    CHECK (d == 0.0f);
}

TEST_CASE ("Hot input can't clip any machine")
{
    // A chord and noise bursts at up to +18 dBFS through every machine, wet only, with the
    // shimmer and feedback knobs up: the wet output stays under its -1 dBFS ceiling.
    const double fs = 48000.0;
    const std::size_t n = static_cast<std::size_t> (4.0 * fs);
    const auto bursts = noise (n, 9);
    std::vector<float> in (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / fs;
        for (double f : { 110.0, 164.8, 220.0, 277.2, 329.6 })
            in[i] += 0.2f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * f * t));
        if (t < 0.5 || (t > 2.5 && t < 2.7)) in[i] += 1.25f * bursts[i];
    }
    for (int m = 0; m < kNumMachines; ++m)
    {
        ReverbEngine e;
        EngineParams p;
        p.machine = m; p.mix = 1.0f; p.killDry = true; p.tone = 1.0f;
        p.machineParams.decaySeconds = 12.0f; p.machineParams.mod = 1.0f;
        p.machineParams.param1 = 1.0f; p.machineParams.param2 = 1.0f;
        e.setParams (p);
        e.prepare (fs, 256);
        std::vector<float> l (n), r (n);
        for (std::size_t i = 0; i < n; ++i) { l[i] = 4.0f * in[i]; r[i] = -4.0f * in[i]; }
        for (std::size_t i = 0; i < n; i += 256)
            e.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        INFO ("machine " << m << " peak " << peak);
        CHECK (allFinite (l));
        CHECK (peak <= ReverbEngine::kWetCeiling * 1.0001f);
    }
}
