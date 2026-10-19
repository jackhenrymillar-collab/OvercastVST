#include "TestHelpers.h"
#include "dsp/Machines.h"
#include "dsp/ReverbEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cstdio>
#include <random>

using namespace overcast::dsp;
using namespace overcast::test;

namespace
{
    const char* kNames[kNumMachines] = { "Chamber", "Arena", "Sheet", "Coil", "Rise", "Blossom",
                                         "Haze", "Choir", "Halo", "Tape Echo", "Gate", "Space Map" };

    MachineParams defaults()
    {
        MachineParams p;
        p.decaySeconds = 3.0f;
        p.mod = 0.5f;
        p.param1 = 0.5f;
        p.param2 = 0.5f;
        p.preDelayMs = 120.0f;
        return p;
    }

    struct Run
    {
        std::vector<float> l, r;
    };

    Run render (ReverbMachine& m, const std::vector<float>& in, int block = 256)
    {
        Run out { std::vector<float> (in.size()), std::vector<float> (in.size()) };
        for (std::size_t i = 0; i < in.size(); i += static_cast<std::size_t> (block))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), in.size() - i));
            m.process (in.data() + i, in.data() + i, out.l.data() + i, out.r.data() + i, n);
        }
        return out;
    }

    // Noise band-limited to 8 kHz, so it has the same content at every sample rate.
    std::vector<float> noiseBurst (double fs, double burstSeconds, double totalSeconds, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-0.5f, 0.5f);
        Biquad lp1, lp2;
        lp1.setLowpass (8000.0f, fs);
        lp2.setLowpass (8000.0f, fs);
        // Scale so the in-band power is the same at every sample rate.
        const auto scale = static_cast<float> (std::sqrt (fs / 48000.0));
        std::vector<float> v (static_cast<std::size_t> (totalSeconds * fs), 0.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t> (burstSeconds * fs); ++i)
            v[i] = scale * lp2.process (lp1.process (d (rng)));
        return v;
    }

    double rmsDb (const std::vector<float>& l, const std::vector<float>& r, std::size_t from, std::size_t to)
    {
        double e = 0.0;
        for (std::size_t i = from; i < to; ++i)
            e += static_cast<double> (l[i]) * l[i] + static_cast<double> (r[i]) * r[i];
        return 10.0 * std::log10 (e / static_cast<double> (2 * (to - from)) + 1e-30);
    }

    std::unique_ptr<ReverbMachine> make (int id, double fs, const MachineParams& p)
    {
        auto m = createMachine (static_cast<MachineId> (id));
        m->setParams (p);
        m->prepare (fs, 256);
        return m;
    }
}

TEST_CASE ("Every machine produces a finite tail from a noise burst")
{
    const int id = GENERATE (range (0, kNumMachines));
    auto m = make (id, 48000.0, defaults());
    const auto out = render (*m, noiseBurst (48000.0, 0.5, 1.5, 21));
    INFO (kNames[id]);
    CHECK (allFinite (out.l));
    CHECK (allFinite (out.r));
    CHECK (energy (out.l.data(), out.l.size()) + energy (out.r.data(), out.r.size()) > 1e-4);
}

TEST_CASE ("Every machine is silent for silent input")
{
    const int id = GENERATE (range (0, kNumMachines));
    auto m = make (id, 48000.0, defaults());
    const auto out = render (*m, std::vector<float> (96000, 0.0f));
    INFO (kNames[id]);
    CHECK (energy (out.l.data(), out.l.size()) == 0.0);
    CHECK (energy (out.r.data(), out.r.size()) == 0.0);
}

TEST_CASE ("Every machine stays stable under random parameter automation")
{
    const int id = GENERATE (range (0, kNumMachines));
    const double fs = 48000.0;
    auto m = make (id, fs, defaults());
    std::mt19937 rng (static_cast<unsigned> (id) + 7);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::vector<float> in (64), l (64), r (64);
    float peak = 0.0f;
    bool finite = true;
    for (int block = 0; block < 10 * 750; ++block) // 10 seconds
    {
        if (block % 12 == 0)
        {
            MachineParams p;
            p.decaySeconds = 0.1f + 59.9f * u (rng) * u (rng);
            p.mod = u (rng);
            p.param1 = u (rng);
            p.param2 = u (rng);
            p.preDelayMs = 500.0f * u (rng);
            p.freeze = u (rng) > 0.85f;
            m->setParams (p);
        }
        for (auto& v : in) v = (u (rng) - 0.5f) * 1.6f;
        m->process (in.data(), in.data(), l.data(), r.data(), 64);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
    }
    INFO (kNames[id] << " peak " << peak);
    CHECK (finite);
    CHECK (peak < 30.0f);
}

TEST_CASE ("Freeze never makes a tail grow, and holds it on sustaining machines")
{
    const int id = GENERATE (range (0, kNumMachines));
    const double fs = 48000.0;
    auto m = make (id, fs, defaults());
    render (*m, noiseBurst (fs, 1.0, 1.0, 3));
    auto p = defaults();
    p.freeze = true;
    m->setParams (p);
    const std::vector<float> silence (static_cast<std::size_t> (fs), 0.0f);
    render (*m, silence);
    const auto first = render (*m, silence);
    Run later;
    for (int s = 0; s < 6; ++s)
        later = render (*m, silence);
    const double e1 = energy (first.l.data(), first.l.size()) + energy (first.r.data(), first.r.size());
    const double e2 = energy (later.l.data(), later.l.size()) + energy (later.r.data(), later.r.size());
    INFO (kNames[id] << ": energy " << e1 << " then " << e2 << " six seconds later");
    CHECK (e2 <= e1 * 1.1 + 1e-12);

    // Gate is a fixed-length pattern, so there is nothing to hold.
    const bool sustains = id != static_cast<int> (MachineId::gate);
    if (sustains)
        CHECK (e2 > e1 * 0.25);
}

TEST_CASE ("Hall-type machines decay at the Decay setting")
{
    const int id = GENERATE (static_cast<int> (MachineId::arena), static_cast<int> (MachineId::haze));
    const float target = GENERATE (1.5f, 4.0f);
    const double fs = 48000.0;
    auto p = defaults();
    p.decaySeconds = target;
    p.param1 = 0.5f;
    p.param2 = 1.0f;
    auto m = make (id, fs, p);
    std::vector<float> in (static_cast<std::size_t> (fs * (target * 1.5 + 1.0)), 0.0f);
    in[0] = 1.0f;
    const auto out = render (*m, in);
    const double measured = rt60 (out.l, fs);
    INFO (kNames[id] << ": target " << target << " s, measured " << measured << " s");
    CHECK (measured == Catch::Approx (target).epsilon (0.25));
}

TEST_CASE ("Every machine sounds the same at 44.1 and 96 kHz")
{
    const int id = GENERATE (range (0, kNumMachines));
    double levels[2] {};
    int k = 0;
    for (double fs : { 44100.0, 96000.0 })
    {
        auto m = make (id, fs, defaults());
        const auto out = render (*m, noiseBurst (fs, 0.5, 2.5, 11));
        levels[k++] = rmsDb (out.l, out.r, static_cast<std::size_t> (0.5 * fs), out.l.size());
    }
    INFO (kNames[id] << ": " << levels[0] << " dB at 44.1 kHz, " << levels[1] << " dB at 96 kHz");
    CHECK (std::abs (levels[0] - levels[1]) < 1.5);
}

TEST_CASE ("Machines are loudness matched")
{
    const double fs = 48000.0;
    const auto in = noiseBurst (fs, 2.0, 4.0, 5);
    double levels[kNumMachines] {};
    for (int id = 0; id < kNumMachines; ++id)
    {
        auto m = make (id, fs, defaults());
        const auto out = render (*m, in);
        levels[id] = rmsDb (out.l, out.r, static_cast<std::size_t> (0.5 * fs), out.l.size())
                   + 20.0 * std::log10 (kMachineTrim[id]);
    }
    const double reference = levels[static_cast<int> (MachineId::sheet)];
    for (int id = 0; id < kNumMachines; ++id)
    {
        // Printed so the trims in Machines.h can be recalibrated after a voicing change.
        std::printf ("loudness %-10s %+6.2f dB vs Sheet (suggested trim %.3f)\n", kNames[id], levels[id] - reference,
                     kMachineTrim[id] * std::pow (10.0, (reference - levels[id]) / 20.0));
        INFO (kNames[id] << " is " << levels[id] - reference << " dB from Sheet");
        CHECK (std::abs (levels[id] - reference) < 2.0);
    }
}

TEST_CASE ("Tape Echo repeats at the Pre-delay time")
{
    const double fs = 48000.0;
    auto p = defaults();
    p.preDelayMs = 250.0f;
    p.param1 = 0.0f; // first head only
    p.param2 = 0.0f; // no feedback
    p.mod = 0.0f;
    auto m = make (static_cast<int> (MachineId::tapeEcho), fs, p);
    std::vector<float> in (static_cast<std::size_t> (fs), 0.0f);
    in[0] = 1.0f;
    const auto out = render (*m, in);
    std::size_t peakAt = 0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < out.l.size(); ++i)
        if (std::abs (out.l[i]) > peak) { peak = std::abs (out.l[i]); peakAt = i; }
    CHECK (static_cast<double> (peakAt) / fs == Catch::Approx (0.25).margin (0.005));
}

TEST_CASE ("Rise swells the dry signal in wet-and-dry mode")
{
    const double fs = 48000.0;
    auto p = defaults();
    p.param1 = 0.4f;  // rise time about 0.25 s
    p.param2 = 1.0f;  // swell the dry signal too
    auto m = make (static_cast<int> (MachineId::rise), fs, p);
    std::vector<float> tone (256), l (256), r (256);
    float early = 0.0f, late = 0.0f;
    for (int b = 0; b < 200; ++b) // about 1 s of a steady note
    {
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.3f * std::sin (0.05f * static_cast<float> (b * 256 + static_cast<int> (i)));
        m->process (tone.data(), tone.data(), l.data(), r.data(), 256);
        REQUIRE (m->dryEnvelope() != nullptr);
        if (b == 1) early = m->dryEnvelope()[0];
        if (b == 199) late = m->dryEnvelope()[255];
    }
    CHECK (early < 0.1f);
    CHECK (late == Catch::Approx (1.0f));
}

TEST_CASE ("Switching machines never clips or produces NaN, with and without spillover")
{
    const bool spill = GENERATE (true, false);
    ReverbEngine engine;
    EngineParams p;
    p.mix = 1.0f;
    p.spillover = spill;
    p.machineParams = defaults();
    engine.setParams (p);
    engine.prepare (48000.0, 512);

    std::mt19937 rng (9);
    std::uniform_int_distribution<int> pick (0, kNumMachines - 1);
    float peak = 0.0f;
    bool finite = true;
    bool sawSpill = false;
    for (int b = 0; b < 600; ++b)
    {
        if (b % 25 == 0)
        {
            p.machine = pick (rng);
            engine.setParams (p);
        }
        std::vector<float> l (512), r (512);
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.4f * std::sin (0.031f * static_cast<float> (b * 512 + static_cast<int> (i)));
        engine.process (l.data(), r.data(), 512);
        sawSpill = sawSpill || engine.isSpilling();
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
    }
    CHECK (finite);
    CHECK (peak <= 1.0f);
    CHECK (sawSpill);
}
