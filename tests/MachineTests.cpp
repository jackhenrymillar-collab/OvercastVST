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
                                         "Haze", "Aurora", "Halo", "Nebula", "Clear Night", "Space Map" };

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

// A tail that rings at fixed pitches sounds metallic. That shows up in the impulse
// response as echoes that repeat at a fixed spacing, so the strongest repeat (the
// autocorrelation peak) has to stay small. Sheet and Haze measure 0.12 to 0.15;
// Chamber and Space Map used to measure 0.3 to 0.45. Coil (spring drip) repeats on
// purpose and is left out.
TEST_CASE ("Tails don't ring with repeating echoes")
{
    struct Setting { int id; float param1; float decay; float param2 = 0.5f; };
    const auto setting = GENERATE (
        Setting { static_cast<int> (MachineId::chamber), 0.0f, 1.4f },
        Setting { static_cast<int> (MachineId::chamber), 0.5f, 1.4f },
        Setting { static_cast<int> (MachineId::chamber), 1.0f, 3.0f },
        Setting { static_cast<int> (MachineId::spaceMap), 0.0f, 1.8f },
        Setting { static_cast<int> (MachineId::spaceMap), 0.6f, 1.8f },
        Setting { static_cast<int> (MachineId::clearNight), 0.0f, 3.0f },
        Setting { static_cast<int> (MachineId::clearNight), 1.0f, 3.0f },
        Setting { static_cast<int> (MachineId::nebula), 0.0f, 3.0f, 0.0f },
        Setting { static_cast<int> (MachineId::nebula), 1.0f, 3.0f, 1.0f },
        Setting { static_cast<int> (MachineId::aurora), 0.5f, 3.0f, 0.0f }, // natural tuning; harmony rings on purpose
        Setting { static_cast<int> (MachineId::sheet), 0.5f, 3.0f },
        Setting { static_cast<int> (MachineId::haze), 0.5f, 3.0f },
        Setting { static_cast<int> (MachineId::arena), 0.0f, 3.0f },
        Setting { static_cast<int> (MachineId::arena), 1.0f, 3.0f },
        Setting { static_cast<int> (MachineId::blossom), 0.5f, 3.0f },
        Setting { static_cast<int> (MachineId::halo), 0.5f, 3.0f });

    const double fs = 24000.0; // the machines are sample-rate independent; this keeps the test quick
    auto p = defaults();
    p.param1 = setting.param1;
    p.param2 = setting.param2;
    p.decaySeconds = setting.decay;
    p.mod = 0.3f;
    auto m = make (setting.id, fs, p);
    std::vector<float> in (static_cast<std::size_t> (fs * 0.7), 0.0f);
    in[0] = 1.0f;
    const auto out = render (*m, in);

    // Low frequencies decay slowest and dominate a late tail, which shows up as a slow
    // repeat that isn't flutter. Look above 200 Hz, where ringing is heard.
    std::vector<float> tail = out.l;
    Biquad hp1, hp2;
    hp1.setHighpass (200.0f, fs);
    hp2.setHighpass (200.0f, fs);
    for (auto& v : tail)
        v = hp2.process (hp1.process (v));

    // 50 ms to 650 ms: past the direct sound, inside the tail.
    const std::size_t start = static_cast<std::size_t> (0.05 * fs), n = static_cast<std::size_t> (0.6 * fs);
    const float* x = tail.data() + start;
    double zero = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        zero += static_cast<double> (x[i]) * x[i];
    REQUIRE (zero > 0.0);

    double worst = 0.0;
    std::size_t worstLag = 0;
    for (std::size_t lag = static_cast<std::size_t> (0.002 * fs); lag < n / 2; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i)
            sum += static_cast<double> (x[i]) * x[i + lag];
        if (std::abs (sum) / zero > worst)
        {
            worst = std::abs (sum) / zero;
            worstLag = lag;
        }
    }
    INFO (kNames[setting.id] << " param1 " << setting.param1 << ": repeat of " << worst
                             << " at " << 1000.0 * static_cast<double> (worstLag) / fs << " ms");
    CHECK (worst < 0.25);
}

// Strongest repeat between 3 and 40 ms of a highpassed tail: with Harmony up, Aurora's
// modes sit on the harmonics of A (55 Hz), so the tail repeats every period of the root.
static double rootPeriodMs (float pitchKnob)
{
    const double fs = 24000.0;
    auto p = defaults();
    p.param1 = pitchKnob;
    p.param2 = 1.0f;
    p.mod = 0.0f;
    auto m = make (static_cast<int> (MachineId::aurora), fs, p);
    std::vector<float> in (static_cast<std::size_t> (fs * 1.2), 0.0f);
    in[0] = 1.0f;
    const auto out = render (*m, in);
    const std::size_t start = static_cast<std::size_t> (0.3 * fs), n = static_cast<std::size_t> (0.8 * fs);
    const float* x = out.l.data() + start;
    double best = 0.0;
    std::size_t bestLag = 0;
    for (std::size_t lag = static_cast<std::size_t> (0.003 * fs); lag < static_cast<std::size_t> (0.04 * fs); ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i)
            sum += static_cast<double> (x[i]) * x[i + lag];
        if (sum > best) { best = sum; bestLag = lag; }
    }
    return 1000.0 * static_cast<double> (bestLag) / fs;
}

TEST_CASE ("Aurora's Harmony and Pitch knobs tune the tail")
{
    CHECK (rootPeriodMs (0.5f) == Catch::Approx (1000.0 / 55.0).epsilon (0.03));  // A
    CHECK (rootPeriodMs (1.0f) == Catch::Approx (1000.0 / 110.0).epsilon (0.03)); // an octave up
}

// Abel and Huang's normalised echo density over 20 ms windows: about 1 once the
// reverb sounds like smooth noise, near 0 while it is still separate echoes.
static double echoDensity (const std::vector<float>& x, std::size_t centre, std::size_t half)
{
    double sq = 0.0;
    for (std::size_t i = centre - half; i < centre + half; ++i)
        sq += static_cast<double> (x[i]) * x[i];
    const double sd = std::sqrt (sq / static_cast<double> (2 * half));
    std::size_t above = 0;
    for (std::size_t i = centre - half; i < centre + half; ++i)
        if (std::abs (x[i]) > sd) ++above;
    return static_cast<double> (above) / static_cast<double> (2 * half) / 0.3173105;
}

TEST_CASE ("Nebula is dense from the start")
{
    // Averaged over 20 to 80 ms, while a delay network is still separate echoes.
    const auto early = [] (MachineId id)
    {
        const double fs = 48000.0;
        auto p = defaults();
        p.param2 = 0.0f;
        auto m = make (static_cast<int> (id), fs, p);
        std::vector<float> in (static_cast<std::size_t> (fs * 0.2), 0.0f);
        in[0] = 1.0f;
        const auto out = render (*m, in);
        const auto half = static_cast<std::size_t> (0.01 * fs);
        double sum = 0.0;
        for (double t : { 0.02, 0.04, 0.06, 0.08 })
            sum += echoDensity (out.l, static_cast<std::size_t> (t * fs), half);
        return sum / 4.0;
    };
    const double nebula = early (MachineId::nebula), arena = early (MachineId::arena);
    INFO ("early echo density: Nebula " << nebula << ", Arena " << arena);
    CHECK (nebula > 0.55);
    CHECK (nebula > arena + 0.25);
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

TEST_CASE ("Moving knobs that change delay lengths doesn't click or zip")
{
    // A 220 Hz sine through the engine while a knob jumps between extremes. Delay
    // lengths and Aurora's modes may glide (a pitch bend) but must never jump or sweep
    // fast; either shows up as a burst of energy far above the sine, so the output
    // above 5 kHz (an 8th-order highpass) must stay quiet.
    enum Knob { first, second, decay };
    struct Case { int machine; Knob knob; };
    const auto c = GENERATE (Case { 0, first },    // Chamber size
                             Case { 11, first },   // Space Map size
                             Case { 1, decay },    // Arena decay
                             Case { 7, first },    // Aurora pitch
                             Case { 7, decay },    // Aurora decay
                             Case { 9, second },   // Nebula softness
                             Case { 10, first });  // Clear Night size
    const int machine = c.machine;
    CAPTURE (kNames[machine], static_cast<int> (c.knob));
    const double fs = 48000.0;
    constexpr int B = 64;
    ReverbEngine e;
    EngineParams ep;
    ep.machine = machine;
    ep.machineParams = defaults();
    ep.machineParams.decaySeconds = 2.0f;
    ep.machineParams.param1 = 0.1f;
    ep.mix = 0.5f;
    ep.preDelayMs = 20.0f;
    e.setParams (ep);
    e.prepare (fs, B);
    float L[B], R[B], worst = 0;
    std::array<Biquad, 4> hp;
    for (auto& f : hp)
        f.setHighpass (5000.0f, fs);
    double ph = 0, acc = 0;
    int count = 0;
    for (int i = 0; i < static_cast<int> (fs * 4); i += B)
    {
        if (i >= static_cast<int> (fs) && (i / B) % 750 == 0)
        {
            const bool high = ((i / B) / 750) % 2;
            if (c.knob == decay)       ep.machineParams.decaySeconds = high ? 10.0f : 0.5f;
            else if (c.knob == first)  ep.machineParams.param1 = high ? 0.9f : 0.1f;
            else                       ep.machineParams.param2 = high ? 0.9f : 0.1f;
        }
        e.setParams (ep);
        for (int k = 0; k < B; ++k)
        {
            L[k] = R[k] = 0.1f * static_cast<float> (std::sin (ph)); // low enough that the output limiter never acts
            ph += 2.0 * 3.14159265358979 * 220.0 / fs;
        }
        e.process (L, R, B);
        for (int k = 0; k < B; ++k)
        {
            float q = L[k];
            for (auto& f : hp)
                q = f.process (q);
            acc += static_cast<double> (q) * q;
            if (++count == 480)
            {
                if (i > fs / 2) worst = std::max (worst, static_cast<float> (std::sqrt (acc / 480.0)));
                acc = 0;
                count = 0;
            }
        }
    }
    CHECK (20.0f * std::log10 (worst + 1.0e-12f) < -70.0f); // -60 dB at the earlier 3x louder test tone
}

// How peaky a slice of a tail's spectrum is, between loHz and hiHz, at 0.5 Hz
// resolution. Each bin's power is divided by the average of its neighbours; for a
// smooth noise-like tail the result scatters like noise and this measures about 1.
// A tail that has split into separate ringing pitches is a row of spikes with gaps
// between them, and measures 5 to 10.
static double ringingPitches (const std::vector<float>& x, double fs, double startSeconds, double loHz, double hiHz)
{
    const std::size_t n = static_cast<std::size_t> (2.0 * fs), start = static_cast<std::size_t> (startSeconds * fs);
    std::vector<double> seg (n);
    for (std::size_t i = 0; i < n; ++i)
        seg[i] = x[start + i] * (0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i) / static_cast<double> (n)));

    const std::size_t k0 = static_cast<std::size_t> (loHz * 2.0), k1 = static_cast<std::size_t> (hiHz * 2.0);
    constexpr std::size_t kHalf = 50;
    std::vector<double> power;
    for (std::size_t k = k0 - kHalf; k < k1 + kHalf; ++k)
    {
        const double w = 6.283185307179586 * static_cast<double> (k) / static_cast<double> (n);
        const double cw = std::cos (w), sw = std::sin (w);
        double re = 0.0, im = 0.0, c = 1.0, s = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            re += seg[i] * c;
            im -= seg[i] * s;
            const double nc = c * cw - s * sw;
            s = s * cw + c * sw;
            c = nc;
        }
        power.push_back (re * re + im * im);
    }

    double sumQ = 0.0, sumQ2 = 0.0;
    for (std::size_t j = kHalf; j + kHalf < power.size(); ++j)
    {
        double local = 0.0;
        for (std::size_t m = j - kHalf; m <= j + kHalf; ++m)
            local += power[m];
        const double q = power[j] / (local / static_cast<double> (2 * kHalf + 1) + 1.0e-30);
        sumQ += q;
        sumQ2 += q * q;
    }
    const double count = static_cast<double> (power.size() - 2 * kHalf);
    return (sumQ2 / count) / ((sumQ / count) * (sumQ / count)) - 1.0;
}

// Long decays used to ring at separate pitches below 1 kHz, right where piano notes
// sit (Chamber and Space Map measured 5 to 10 at 9 s, with Mod at 30% or off). The
// network's mixing matrix now turns faster as the decay grows, which keeps the tail
// smooth at every Mod setting.
TEST_CASE ("Long tails don't ring at separate pitches")
{
    const auto id = GENERATE (static_cast<int> (MachineId::chamber), static_cast<int> (MachineId::arena),
                              static_cast<int> (MachineId::rise), static_cast<int> (MachineId::blossom),
                              static_cast<int> (MachineId::haze), static_cast<int> (MachineId::halo),
                              static_cast<int> (MachineId::nebula), static_cast<int> (MachineId::clearNight),
                              static_cast<int> (MachineId::spaceMap));
    const float mod = GENERATE (0.0f, 0.3f);

    const double fs = 16000.0;
    auto p = defaults();
    p.decaySeconds = 12.0f;
    p.mod = mod;
    p.param1 = 0.5f;
    p.param2 = 0.5f;
    auto m = make (id, fs, p);
    std::vector<float> in (static_cast<std::size_t> (fs * 5.5), 0.0f);
    in[0] = 1.0f;
    const auto out = render (*m, in);
    std::vector<float> sum (out.l.size());
    for (std::size_t i = 0; i < sum.size(); ++i)
        sum[i] = out.l[i] + out.r[i];

    for (const auto band : { std::pair { 100.0, 400.0 }, std::pair { 400.0, 1000.0 } })
    {
        const double peaky = ringingPitches (sum, fs, 3.0, band.first, band.second);
        INFO (kNames[id] << " with Mod " << mod << ", " << band.first << " to " << band.second << " Hz: " << peaky);
        CHECK (peaky < 2.5);
    }
}
