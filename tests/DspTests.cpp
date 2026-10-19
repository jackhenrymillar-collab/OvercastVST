#include "dsp/Blocks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace overcast::dsp;
using Catch::Approx;

TEST_CASE ("Delay line returns the sample pushed d samples ago")
{
    DelayLine d;
    d.prepare (16);
    for (int i = 1; i <= 10; ++i)
        d.push (static_cast<float> (i));
    CHECK (d.read (1) == 10.0f);
    CHECK (d.read (4) == 7.0f);
}

TEST_CASE ("Lagrange read is exact on integer delays and interpolates a ramp exactly")
{
    DelayLine d;
    d.prepare (32);
    for (int i = 0; i < 20; ++i)
        d.push (static_cast<float> (i));
    CHECK (d.readFractional (5.0f) == Approx (d.read (5)));
    // A linear ramp is reproduced exactly by cubic interpolation.
    CHECK (d.readFractional (5.25f) == Approx (19.0f - 4.25f).margin (1e-4));
}

TEST_CASE ("Allpass preserves energy")
{
    Allpass ap;
    ap.prepare (64);
    std::vector<float> out (20000);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = ap.process (i == 0 ? 1.0f : 0.0f, 37, 0.7f);
    double e = 0.0;
    for (float v : out)
        e += static_cast<double> (v) * v;
    CHECK (e == Approx (1.0).margin (1e-4));
}

TEST_CASE ("Soft limiter is transparent below threshold and bounded above")
{
    CHECK (softLimit (0.5f) == 0.5f);
    CHECK (softLimit (-0.89f) == -0.89f);
    CHECK (softLimit (10.0f) <= 1.0f);
    CHECK (softLimit (-10.0f) >= -1.0f);
    CHECK (softLimit (0.95f) < 0.95f);
}

TEST_CASE ("Smoother converges to its target")
{
    Smoother s;
    s.prepare (48000.0, 10.0f);
    s.snapTo (0.0f);
    s.setTarget (1.0f);
    for (int i = 0; i < 48000; ++i)
        s.next();
    CHECK (s.getCurrent() == 1.0f);
}

TEST_CASE ("Biquad low cut removes DC")
{
    Biquad hp;
    hp.setHighpass (100.0f, 48000.0);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
        y = hp.process (1.0f);
    CHECK (std::abs (y) < 1e-4f);
}
