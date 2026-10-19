#include "TestHelpers.h"
#include "dsp/ReverbEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
