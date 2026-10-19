#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

#include <cstdint>

namespace overcast::dsp
{

// Nebula: velvet-noise feedback delay network (Fagerström, Alary, Schlecht and
// Välimäki, "Velvet-noise feedback delay network", DAFx 2020; dark velvet noise from
// Fagerström, Schlecht and Välimäki, DAFx 2022). Each of the 16 lines is fed and
// tapped through its own sparse velvet-noise filter: a few dozen randomly placed
// pulses of +1 or -1. Sixteen different filters on the way in and out make the
// reverb fully dense from the first moment, so it washes in with no grainy build-up.
// In dark velvet noise, later pulses are progressively low-passed, so the onset
// blooms from bright to dark.
// param1 = darkness, param2 = softness (how slowly the sound swells in: from a quick
// wash to a long, soft bloom of up to a quarter of a second).
class Nebula final : public ReverbMachine
{
public:
    static constexpr std::size_t kLines = 16;
    static constexpr std::size_t kInTaps = 24;   // per line, over up to kInMs
    static constexpr std::size_t kOutTaps = 8;   // per line and channel, over kOutMs
    static constexpr std::size_t kGroups = 3;    // dark velvet: early, middle and late pulses
    static constexpr std::size_t kPerGroup = kInTaps / kGroups;
    static constexpr float kInMs = 260.0f;
    static constexpr float kOutMs = 23.0f;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        inL.prepare (static_cast<std::size_t> (msToSamples (kInMs, fs)) + 8);
        inR.prepare (static_cast<std::size_t> (msToSamples (kInMs, fs)) + 8);
        for (auto& h : history)
            h.prepare (static_cast<std::size_t> (msToSamples (kOutMs, fs)) + 10);
        fdn.prepare (fs, kDelays.back() * 1.05f);
        fdn.setDelays (kDelays);
        fdn.setLoopAllpasses (kLoopAllpass, 0.5f);
        layoutTaps();
        cutoffs.fill (0.5f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        inL.reset(); inR.reset();
        for (auto& h : history) h.reset();
        for (auto& line : groupLp) for (auto& f : line) f.reset();
        fdn.reset();
        inGain = inTarget;
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float dark = std::clamp (p.param1, 0.0f, 1.0f);
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 1.15f;
        d.rtHigh = p.decaySeconds * (0.75f - 0.5f * dark);
        d.highHz = 5000.0f - 2500.0f * dark;
        fdn.setDecay (d);
        fdn.setModulation (p.mod);
        fdn.setFreeze (p.freeze);

        // Dark velvet noise: each later group of pulses is low-passed harder.
        const float nyq = 0.5f * static_cast<float> (fs);
        const std::array<float, kGroups> hz { std::min (nyq * 0.9f, 16000.0f - 6000.0f * dark),
                                              8000.0f - 5000.0f * dark,
                                              4000.0f - 2800.0f * dark };
        for (std::size_t g = 0; g < kGroups; ++g)
            cutoffs[g] = OnePoleLowpass::coefficientFor (hz[g], fs);

        // Softness reshapes the pulse envelope: a fast decaying burst (soft = 0) through
        // to a slow swell that peaks late (soft = 1). Gains glide; pulse times never move.
        const float soft = std::clamp (p.param2, 0.0f, 1.0f);
        for (std::size_t i = 0; i < kLines; ++i)
        {
            std::array<float, kInTaps> env {};
            float energy = 0.0f;
            for (std::size_t k = 0; k < kInTaps; ++k)
            {
                const float t = inTapMs[i][k] / kInMs; // 0..1
                const float burst = std::exp (-t * 6.0f);
                const float swell = std::pow (t, 1.2f) * std::exp (-t * 2.5f);
                env[k] = burst + soft * (swell * 6.0f - burst);
                energy += env[k] * env[k];
            }
            const float norm = 1.0f / std::sqrt (std::max (energy, 1.0e-9f));
            for (std::size_t k = 0; k < kInTaps; ++k)
                inTarget[i][k] = inSign[i][k] * env[k] * norm;
        }
    }

    void process (const float* l, const float* r, float* wetL, float* wetR, int n) override
    {
        // Pulse gains glide toward their targets once per block (about 60 ms to settle),
        // stepping in a straight line across the block so a softness change never clicks.
        const float follow = 1.0f - std::exp (-static_cast<float> (n) / (0.02f * static_cast<float> (fs)));
        const float perSample = 1.0f / static_cast<float> (std::max (n, 1));
        for (std::size_t i = 0; i < kLines; ++i)
            for (std::size_t k = 0; k < kInTaps; ++k)
                inStep[i][k] = (inTarget[i][k] - inGain[i][k]) * follow * perSample;

        for (int s = 0; s < n; ++s)
        {
            inL.push (l[s]);
            inR.push (r[s]);

            std::array<float, kLines> feed {}, out {};
            for (std::size_t i = 0; i < kLines; ++i)
            {
                const DelayLine& src = (i % 2 == 0) ? inL : inR;
                float sum = 0.0f;
                for (std::size_t g = 0; g < kGroups; ++g)
                {
                    // Taps are in time order, so each group is a run of kPerGroup taps.
                    float group = 0.0f;
                    for (std::size_t k = g * kPerGroup; k < (g + 1) * kPerGroup; ++k)
                    {
                        inGain[i][k] += inStep[i][k];
                        group += inGain[i][k] * src.read (inTapSamples[i][k]);
                    }
                    sum += groupLp[i][g].process (group, cutoffs[g]);
                }
                feed[i] = sum;
            }

            fdn.processLines (feed, out);

            float yl = 0.0f, yr = 0.0f;
            for (std::size_t i = 0; i < kLines; ++i)
            {
                // The velvet-filtered input also goes straight to the output filters, so
                // the wash starts at once instead of waiting a trip round the network.
                history[i].push (out[i] + kDirect * feed[i]);
                for (std::size_t k = 0; k < kOutTaps; ++k)
                {
                    yl += outGainL[i][k] * history[i].read (outTapL[i][k]);
                    yr += outGainR[i][k] * history[i].read (outTapR[i][k]);
                }
            }
            wetL[s] = yl * kOutputScale;
            wetR[s] = yr * kOutputScale;
        }
    }

private:
    void layoutTaps()
    {
        std::uint32_t seed = 0x6a09e667u;
        const auto next = [&seed]
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return static_cast<float> (seed >> 8) / 16777216.0f;
        };
        for (std::size_t i = 0; i < kLines; ++i)
        {
            for (std::size_t k = 0; k < kInTaps; ++k)
            {
                // Velvet noise: one pulse at a random point in each slot. The slots are
                // packed tighter early on (t = u^2), so a short burst still has pulses.
                const float u = (static_cast<float> (k) + next()) / static_cast<float> (kInTaps);
                const float ms = 0.3f + u * u * (kInMs - 0.5f);
                inTapMs[i][k] = ms;
                inTapSamples[i][k] = static_cast<std::size_t> (std::max (1.0f, std::round (msToSamples (ms, fs))));
                inSign[i][k] = next() < 0.5f ? -1.0f : 1.0f;
            }
            float eL = 0.0f, eR = 0.0f;
            for (std::size_t k = 0; k < kOutTaps; ++k)
            {
                const float uL = (static_cast<float> (k) + next()) / static_cast<float> (kOutTaps);
                const float uR = (static_cast<float> (k) + next()) / static_cast<float> (kOutTaps);
                outTapL[i][k] = static_cast<std::size_t> (1.0f + std::round (msToSamples (uL * kOutMs, fs))); // read (1) is the newest sample
                outTapR[i][k] = static_cast<std::size_t> (1.0f + std::round (msToSamples (uR * kOutMs, fs)));
                outGainL[i][k] = (next() < 0.5f ? -1.0f : 1.0f) * std::exp (-uL * 1.5f);
                outGainR[i][k] = (next() < 0.5f ? -1.0f : 1.0f) * std::exp (-uR * 1.5f);
                eL += outGainL[i][k] * outGainL[i][k];
                eR += outGainR[i][k] * outGainR[i][k];
            }
            for (std::size_t k = 0; k < kOutTaps; ++k)
            {
                outGainL[i][k] /= std::sqrt (eL);
                outGainR[i][k] /= std::sqrt (eR);
            }
        }
    }

    static constexpr std::array<float, kLines> kDelays { 31.3f, 34.7f, 37.9f, 41.3f, 44.9f, 48.1f, 52.7f, 56.3f,
                                                         60.1f, 64.7f, 68.9f, 73.1f, 77.9f, 82.3f, 87.7f, 93.1f };
    static constexpr std::array<float, kLines> kLoopAllpass { 2.31f, 4.07f, 1.63f, 3.37f, 2.83f, 1.21f, 3.91f, 2.17f,
                                                              4.41f, 1.87f, 3.03f, 2.59f, 1.39f, 3.61f, 2.03f, 4.23f };
    static constexpr float kOutputScale = 0.1f;
    static constexpr float kDirect = 0.6f;

    double fs = 48000.0;
    DelayLine inL, inR;
    std::array<DelayLine, kLines> history;
    Fdn<kLines> fdn;
    std::array<std::array<float, kInTaps>, kLines> inTapMs {}, inSign {};
    std::array<std::array<std::size_t, kInTaps>, kLines> inTapSamples {};
    std::array<std::array<float, kInTaps>, kLines> inGain {}, inTarget {}, inStep {};
    std::array<std::array<OnePoleLowpass, kGroups>, kLines> groupLp;
    std::array<float, kGroups> cutoffs {};
    std::array<std::array<std::size_t, kOutTaps>, kLines> outTapL {}, outTapR {};
    std::array<std::array<float, kOutTaps>, kLines> outGainL {}, outGainR {};
    MachineParams params;
};

} // namespace overcast::dsp
