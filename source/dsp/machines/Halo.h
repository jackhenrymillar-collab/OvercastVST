#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Halo: shimmer. Pitch shifters sit inside the feedback network on two of its eight lines,
// so every trip round the loop climbs another interval and the Hadamard mix spreads the
// shifted sound through the whole tail.
// param1 = interval (-12, +5, +7, +12, +19 semitones), param2 = shimmer amount.
//
// Stability: blending a line with its pitch-shifted copy loses energy (the two are at
// different pitches, so they don't add up), which would cut the tail short. A make-up gain
// on every line restores most of it, but is capped so no line's gain ever reaches one: the
// loop stays stable however the shifted and original signals line up.
class Halo final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        diffL.prepare (fs, { 1.3f, 2.1f, 3.1f, 4.3f, 6.1f, 8.3f });
        diffR.prepare (fs, { 1.5f, 2.3f, 2.9f, 4.7f, 5.9f, 8.9f });
        fdn.prepare (fs, kDelays.back());
        fdn.setDelays (kDelays);
        fdn.setLoopAllpasses (kLoopAllpass, 0.6f);
        shifters[0].prepare (fs, 61.0f);
        shifters[1].prepare (fs, 71.0f);
        amount.prepare (fs, 40.0f);
        makeup.prepare (fs, 40.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        diffL.reset(); diffR.reset(); fdn.reset();
        for (auto& s : shifters) s.reset();
        for (auto& f : tone) f.reset();
        amount.snapTo (amount.getTarget());
        makeup.snapTo (makeup.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 0.9f;
        d.rtHigh = p.decaySeconds * 0.55f;
        fdn.setDecay (d);
        fdn.setModulation (p.mod);
        fdn.setFreeze (p.freeze);
        const auto step = std::min<std::size_t> (static_cast<std::size_t> (std::clamp (p.param1, 0.0f, 1.0f) * 5.0f), 4);
        for (auto& s : shifters)
            s.setRatio (kRatios[step]);

        // Frozen, the shimmer stops climbing so the held chord stays put and lossless.
        const float s = p.freeze ? 0.0f : std::clamp (p.param2, 0.0f, 1.0f);
        amount.setTarget (s);

        // Average energy kept per pass: 6 plain lines, 2 blended lines (the shifter's two
        // heads also lose about a quarter of the energy of what passes through them).
        const float blended = (1.0f - s) * (1.0f - s) + 0.75f * s * s;
        const float kept = (6.0f + 2.0f * blended) / 8.0f;
        const float largestLineGain = decayGain (msToSamples (kDelays.front() + kLoopAllpass.front(), fs),
                                                 std::max ({ d.rtLow, d.rtMid, d.rtHigh }), fs);
        makeup.setTarget (p.freeze ? 1.0f : std::min (1.0f / std::sqrt (kept), 0.998f / largestLineGain));
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const float toneA = OnePoleLowpass::coefficientFor (6500.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float s = amount.next(), c = makeup.next();
            const float l = diffL.process (inL[i], 0.7f);
            const float r = diffR.process (inR[i], 0.7f);
            fdn.process (l, r, wetL[i], wetR[i], [&] (std::size_t line, float v) {
                if (line >= 2)
                    return c * v;
                // Keep the climbing shimmer smooth rather than glassy.
                const float shifted = tone[line].process (shifters[line].process (v), toneA);
                return c * ((1.0f - s) * v + s * shifted);
            });
        }
    }

private:
    // Long lines: fewer passes per second means the shimmer tail lasts longer.
    // Prime sample counts at 48 kHz, with no two lines near a 2:1, 3:2 or 4:3 ratio.
    static constexpr std::array<float, 8> kDelays { 61.2292f, 71.8542f, 79.1042f, 89.3542f, 97.7292f, 108.5208f, 119.6458f, 131.7292f };
    // With so few, long lines the echoes are far apart and repeat audibly when Mod is
    // low; an allpass in each loop fills the gaps between them.
    static constexpr std::array<float, 8> kLoopAllpass { 3.31f, 7.13f, 4.57f, 6.29f, 2.83f, 5.47f, 7.61f, 3.97f };
    static constexpr std::array<float, 5> kRatios { 0.5f, 1.33484f, 1.49831f, 2.0f, 2.99661f };

    double fs = 48000.0;
    Diffuser<6> diffL, diffR; // short stages (under 10 ms): dense without colouring the sound
    Fdn<8> fdn;
    std::array<PitchShifter, 2> shifters;
    std::array<OnePoleLowpass, 2> tone;
    Smoother amount, makeup;
    MachineParams params;
};

} // namespace overcast::dsp
