#include "dsp/Blocks.h"
#include "dsp/Shared.h"
#include "dsp/machines/ColourlessTuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

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

TEST_CASE ("Peak limiter passes quiet signals unchanged and never lets loud ones past the ceiling")
{
    PeakLimiter lim;
    lim.prepare (48000.0, 0.89f);
    const auto look = lim.latency();
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    // Quiet: an exact copy, delayed by the lookahead.
    std::vector<float> in (20000);
    for (auto& x : in) x = 0.5f * u (rng);
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        float l = in[i], r = -in[i];
        lim.process (l, r);
        if (i >= look) { REQUIRE (l == in[i - look]); REQUIRE (r == -in[i - look]); }
    }

    // Loud: bursts up to +18 dBFS with sudden single-sample spikes; nothing above the ceiling.
    lim.reset();
    float worst = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        float l = (i / 4000) % 2 ? 8.0f * u (rng) : 0.3f * u (rng);
        if (i % 7919 == 0) l = 8.0f;
        float r = 0.5f * l;
        lim.process (l, r);
        worst = std::max ({ worst, std::abs (l), std::abs (r) });
    }
    CHECK (worst <= 0.89f * 1.0001f);
}

TEST_CASE ("Loop guard is transparent in normal use, caps runaways and drops NaN")
{
    CHECK (loopGuard (0.5f) == 0.5f);
    CHECK (loopGuard (-30.0f) == -30.0f);
    CHECK (loopGuard (64.0f) == 64.0f);
    CHECK (loopGuard (1.0e6f) <= 128.0f);
    CHECK (loopGuard (-1.0e6f) >= -128.0f);
    CHECK (loopGuard (std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK (loopGuard (std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST_CASE ("Feedback matrices are orthogonal, so they never add gain")
{
    // Hadamard: transform each unit vector and check the columns are orthonormal.
    std::array<std::array<float, 16>, 16> h {};
    for (std::size_t i = 0; i < 16; ++i)
    {
        h[i][i] = 1.0f;
        hadamard (h[i]);
    }
    const auto& a = ColourlessTuning::matrix;
    for (std::size_t i = 0; i < 16; ++i)
        for (std::size_t j = 0; j < 16; ++j)
        {
            double dh = 0.0, dc = 0.0;
            for (std::size_t k = 0; k < 16; ++k)
            {
                dh += static_cast<double> (h[i][k]) * h[j][k];
                dc += static_cast<double> (a[k * 16 + i]) * a[k * 16 + j]; // (A^T A)_ij
            }
            const double want = i == j ? 1.0 : 0.0;
            CHECK (dh == Approx (want).margin (1e-6));
            CHECK (dc == Approx (want).margin (1e-5));
        }
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

namespace
{
    // Output level relative to input (dB) of a pitch shifter in 20 ms slices, once settled.
    std::vector<float> shifterSliceLevels (float ratio, bool noise)
    {
        constexpr double fs = 48000.0;
        PitchShifter ps;
        ps.prepare (fs, 61.0f);
        ps.setRatio (ratio);
        OnePoleLowpass lp;
        const float lpA = OnePoleLowpass::coefficientFor (6000.0f, fs); // like a reverb tail
        std::uint32_t seed = 12345;
        // For the sine, pick a pitch where the two heads (half a grain apart) read in step,
        // the case where power windows alone would bulge.
        const double lag = 0.5 * 61.0 * 0.001 * fs;
        const double hz = 4.0 * fs / lag;
        std::vector<float> levels;
        double outAcc = 0.0, inAcc = 0.0;
        const int slice = 960;
        for (int i = 0; i < 10 * 48000; ++i)
        {
            float x;
            if (noise)
            {
                seed = seed * 1664525u + 1013904223u;
                x = lp.process (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f, lpA);
            }
            else
                x = static_cast<float> (std::sin (2.0 * 3.14159265358979 * hz * i / fs));
            const float y = ps.process (x);
            outAcc += static_cast<double> (y) * y;
            inAcc += static_cast<double> (x) * x;
            if ((i + 1) % slice == 0)
            {
                if (i > 48000)
                    levels.push_back (static_cast<float> (10.0 * std::log10 (outAcc / inAcc)));
                outAcc = inAcc = 0.0;
            }
        }
        return levels;
    }
}

TEST_CASE ("Pitch shifter keeps a steady level, whether its heads are alike or not")
{
    for (float ratio : { 0.5f, 1.33484f, 1.49831f, 2.0f, 2.99661f })
    {
        CAPTURE (ratio);
        // Noise: the heads read unrelated sound, so the power windows must sum to one.
        // (The old sin^2 windows lost 1.8 dB on average and dipped up to 4 dB.)
        const auto n = shifterSliceLevels (ratio, true);
        const auto [nMin, nMax] = std::minmax_element (n.begin(), n.end());
        double mean = 0.0;
        for (float v : n) mean += v;
        mean /= static_cast<double> (n.size());
        CHECK (std::abs (mean) < 1.0);
        CHECK (*nMax - *nMin < 2.2f);

        // A sine the heads read in step: amplitudes add, and must not bulge.
        const auto s = shifterSliceLevels (ratio, false);
        const auto [sMin, sMax] = std::minmax_element (s.begin(), s.end());
        CHECK (*sMax < 0.75f);
        CHECK (*sMin > -1.0f);
    }
}
