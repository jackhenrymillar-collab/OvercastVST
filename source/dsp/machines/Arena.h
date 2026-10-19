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
    void prepare (double sampleRate, int) override { core.prepare (sampleRate); setParams (params); core.reset(); }
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
