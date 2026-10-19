#include "TestHelpers.h"
#include "dsp/machines/PlateReverb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>

using namespace overcast::dsp;
using namespace overcast::test;

namespace
{
    struct Ir
    {
        std::vector<float> left, right;
    };

    Ir impulseResponse (PlateReverb& plate, double fs, double seconds)
    {
        const auto n = static_cast<std::size_t> (seconds * fs);
        std::vector<float> in (n, 0.0f);
        in[0] = 1.0f;
        Ir ir { std::vector<float> (n), std::vector<float> (n) };
        constexpr int block = 512;
        for (std::size_t i = 0; i < n; i += block)
        {
            const int len = static_cast<int> (std::min<std::size_t> (block, n - i));
            plate.process (in.data() + i, in.data() + i, ir.left.data() + i, ir.right.data() + i, len);
        }
        return ir;
    }

    PlateReverb makePlate (double fs, float decay, float damping = 0.0f, float mod = 0.0f)
    {
        PlateReverb p;
        MachineParams mp;
        mp.decaySeconds = decay;
        mp.param1 = 0.0f;
        mp.param2 = damping;
        mp.mod = mod;
        p.setParams (mp);
        p.prepare (fs, 512);
        return p;
    }
}

TEST_CASE ("Plate decay matches the Decay setting")
{
    const float target = GENERATE (1.0f, 2.5f, 6.0f);
    const double fs = 48000.0;
    auto plate = makePlate (fs, target);
    const auto ir = impulseResponse (plate, fs, target * 1.5 + 1.0);
    const double measured = rt60 (ir.left, fs);
    INFO ("target " << target << " s, measured " << measured << " s");
    CHECK (measured == Catch::Approx (target).epsilon (0.2));
}

TEST_CASE ("Plate decay is the same at every sample rate")
{
    const double base = [] {
        auto p = makePlate (48000.0, 2.0f, 0.3f);
        return rt60 (impulseResponse (p, 48000.0, 4.0).left, 48000.0);
    }();
    const double fs = GENERATE (44100.0, 88200.0, 96000.0, 192000.0);
    auto plate = makePlate (fs, 2.0f, 0.3f);
    const double measured = rt60 (impulseResponse (plate, fs, 4.0).left, fs);
    INFO ("fs " << fs << ": " << measured << " s vs " << base << " s at 48 kHz");
    CHECK (measured == Catch::Approx (base).epsilon (0.05));
}

TEST_CASE ("Plate output is decorrelated between left and right")
{
    auto plate = makePlate (48000.0, 2.0f);
    const auto ir = impulseResponse (plate, 48000.0, 2.0);
    CHECK (std::abs (correlation (ir.left, ir.right)) < 0.3);
}

TEST_CASE ("Plate freeze holds the tail without decaying or growing")
{
    const double fs = 48000.0;
    auto plate = makePlate (fs, 2.0f, 0.5f, 0.5f);

    // Excite with a burst of noise, then freeze.
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    std::vector<float> in (static_cast<std::size_t> (fs)), wl (in.size()), wr (in.size());
    for (auto& v : in) v = dist (rng);
    plate.process (in.data(), in.data(), wl.data(), wr.data(), static_cast<int> (in.size()));

    MachineParams mp;
    mp.decaySeconds = 2.0f; mp.param2 = 0.5f; mp.mod = 0.5f; mp.freeze = true;
    plate.setParams (mp);

    std::fill (in.begin(), in.end(), 0.0f);
    // Let the freeze ramp settle, then measure energy over successive seconds.
    plate.process (in.data(), in.data(), wl.data(), wr.data(), static_cast<int> (in.size()));
    plate.process (in.data(), in.data(), wl.data(), wr.data(), static_cast<int> (in.size()));
    const double first = energy (wl.data(), wl.size());
    for (int s = 0; s < 8; ++s)
        plate.process (in.data(), in.data(), wl.data(), wr.data(), static_cast<int> (in.size()));
    const double later = energy (wl.data(), wl.size());

    INFO ("energy first " << first << ", 8 s later " << later);
    CHECK (first > 1e-3);
    CHECK (later / first > 0.5);  // held (slight loss from interpolation is fine)
    CHECK (later / first < 1.05); // never grows
}

TEST_CASE ("Plate stays stable under random parameter automation")
{
    const double fs = 48000.0;
    PlateReverb plate;
    plate.prepare (fs, 64);
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    std::vector<float> in (64), wl (64), wr (64);
    float peak = 0.0f;
    bool finite = true;
    for (int block = 0; block < 20 * 750; ++block) // 20 seconds
    {
        if (block % 10 == 0)
        {
            MachineParams mp;
            mp.decaySeconds = 0.1f + 59.9f * u (rng);
            mp.mod = u (rng);
            mp.param1 = u (rng);
            mp.param2 = u (rng);
            mp.freeze = u (rng) > 0.8f;
            plate.setParams (mp);
        }
        for (auto& v : in) v = (u (rng) - 0.5f) * 1.6f;
        plate.process (in.data(), in.data(), wl.data(), wr.data(), 64);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (wl[i]) && std::isfinite (wr[i]);
            peak = std::max ({ peak, std::abs (wl[i]), std::abs (wr[i]) });
        }
    }
    INFO ("peak " << peak);
    CHECK (finite);
    CHECK (peak < 20.0f);
}
