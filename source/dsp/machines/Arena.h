#pragma once

#include "../ReverbMachine.h"
#include "HallCore.h"

namespace overcast::dsp
{

// Arena: a large hall. Decay sets the mid band; param1 scales the low-band decay
// (0.5x to 2x), param2 scales the high-band decay (0.15x to 1x).
class Arena final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        core.prepare (sampleRate);
        // Extra long-tail movement for Arena, which scored 4.4 for ringing at 12 s of decay.
        // The usual 1 + 0.5 x (score - 4) gives 1.2, but on piano that measured worse (5.0);
        // 1.5 brings it to 2.8. Its delay drift stays uncapped: with the 8 cent cap Arena
        // measured 3.6 and Jack preferred it without.
        core.setTailMovement (1.5f, false);
        setParams (params);
        core.reset();
    }
    void reset() override { core.reset(); }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float low = std::pow (2.0f, 2.0f * std::clamp (p.param1, 0.0f, 1.0f) - 1.0f);
        const float high = 0.15f + 0.85f * std::clamp (p.param2, 0.0f, 1.0f);
        core.configure (p.decaySeconds, low, high, p.mod, 1.5f, p.freeze);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        for (int i = 0; i < n; ++i)
            core.process (inL[i], inR[i], wetL[i], wetR[i]);
    }

private:
    HallCore core;
    MachineParams params;
};

} // namespace overcast::dsp
