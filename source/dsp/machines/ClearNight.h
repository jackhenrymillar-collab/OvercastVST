#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"
#include "ColourlessTuning.h"

namespace overcast::dsp
{

// Clear Night: a colourless feedback delay network (Dal Santo, Prawda, Schlecht and
// Välimäki, "Differentiable feedback delay network for colorless reverberation",
// DAFx 2023). The 16-line network's mixing matrix and its input and output gains were
// optimised offline (see ColourlessTuning.h) so its response is as flat as possible:
// no pitch rings out of the tail, so it stays clean and neutral even at very long
// decays. A short diffuser in front fills in the first 50 ms.
// param1 = size (0.6x to 1.6x the network's delays), param2 = brightness.
class ClearNight final : public ReverbMachine
{
public:
    static constexpr std::size_t kLines = 16;
    static constexpr float kMinSize = 0.6f, kMaxSize = 1.6f;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        diffL.prepare (fs, { 1.3f, 2.1f, 3.1f, 4.3f, 6.1f, 8.3f });
        diffR.prepare (fs, { 1.5f, 2.3f, 2.9f, 4.7f, 5.9f, 8.9f });
        fdn.prepare (fs, ColourlessTuning::delayMs.back() * kMaxSize);
        fdn.setMatrix (&ColourlessTuning::matrix);
        setParams (params);
        reset();
    }

    void reset() override
    {
        diffL.reset(); diffR.reset(); fdn.reset();
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float size = kMinSize + (kMaxSize - kMinSize) * std::clamp (p.param1, 0.0f, 1.0f);
        std::array<float, kLines> ms {};
        for (std::size_t i = 0; i < kLines; ++i)
            ms[i] = ColourlessTuning::delayMs[i] * size;
        fdn.setDelays (ms);

        const float bright = std::clamp (p.param2, 0.0f, 1.0f);
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 1.05f;
        d.rtHigh = p.decaySeconds * (0.3f + 0.65f * bright);
        d.highHz = 2500.0f + 3500.0f * bright;
        fdn.setDecay (d);
        // Gentle by default: the network is already smooth, so Mod only adds motion.
        fdn.setModulation (0.6f * p.mod, 0.3f);
        fdn.setFreeze (p.freeze);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const auto& b = ColourlessTuning::inputGain;
        const auto& cl = ColourlessTuning::outputL;
        const auto& cr = ColourlessTuning::outputR;
        std::array<float, kLines> feed {}, out {};
        for (int s = 0; s < n; ++s)
        {
            // The optimised network has one input: the two diffused channels are summed.
            const float x = 0.70710678f * (diffL.process (inL[s], 0.6f) + diffR.process (inR[s], 0.6f));
            for (std::size_t i = 0; i < kLines; ++i)
                feed[i] = b[i] * x;
            fdn.processLines (feed, out);
            float l = 0.0f, r = 0.0f;
            for (std::size_t i = 0; i < kLines; ++i)
            {
                l += cl[i] * out[i];
                r += cr[i] * out[i];
            }
            wetL[s] = l * kOutputScale;
            wetR[s] = r * kOutputScale;
        }
    }

private:
    static constexpr float kOutputScale = 2.6f;

    double fs = 48000.0;
    Diffuser<6> diffL, diffR; // short stages (under 10 ms): dense without colouring the sound
    Fdn<kLines> fdn;
    MachineParams params;
};

} // namespace overcast::dsp
