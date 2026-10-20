#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Haze: an enormous, diffuse ambient wash. Eight long diffusers per channel feed a
// 16-line FDN. param1 = diffusion, param2 = smear (modulation inside the diffusers).
class Haze final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        diffL.prepare (sampleRate, { 1.3f, 1.9f, 2.7f, 3.4f, 4.1f, 4.9f, 5.6f, 6.3f, 7.1f, 7.9f, 8.6f, 9.4f });
        diffR.prepare (sampleRate, { 1.5f, 2.1f, 2.5f, 3.2f, 4.3f, 4.7f, 5.3f, 6.7f, 7.3f, 8.3f, 8.9f, 9.7f });
        fdn.prepare (sampleRate, kDelays.back());
        fdn.setDelays (kDelays);
        diffusion.prepare (sampleRate, 50.0f);
        smear.prepare (sampleRate, 50.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        diffL.reset(); diffR.reset(); fdn.reset();
        diffusion.snapTo (diffusion.getTarget()); smear.snapTo (smear.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 1.2f;
        d.rtHigh = p.decaySeconds * 0.6f;
        d.highHz = 3500.0f;
        fdn.setDecay (d);
        fdn.setModulation (0.3f + 0.7f * p.mod, 1.0f); // the drifting pitch is part of Haze's sound
        fdn.setFreeze (p.freeze);
        diffusion.setTarget (0.5f + 0.25f * std::clamp (p.param1, 0.0f, 1.0f));
        smear.setTarget (2.5f * std::clamp (p.param2, 0.0f, 1.0f));
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float c = diffusion.next(), s = smear.next();
            const float l = diffL.process (inL[i], c, 1.0f, s);
            const float r = diffR.process (inR[i], c, 1.0f, s);
            fdn.process (l, r, wetL[i], wetR[i]);
        }
    }

private:
    // Prime sample counts at 48 kHz, with no two lines near a 2:1, 3:2 or 4:3 ratio.
    static constexpr std::array<float, 16> kDelays { 61.2292f, 67.8958f, 71.1042f, 77.8958f, 83.3542f, 90.1458f, 94.1042f, 101.2708f,
                                                     107.8958f, 113.2708f, 119.6458f, 127.1042f, 133.7708f, 139.1458f, 146.2292f, 153.5208f };
    Diffuser<12> diffL, diffR; // many short stages (under 10 ms) rather than a few long ones
    Fdn<16> fdn;
    Smoother diffusion, smear;
    MachineParams params;
};

} // namespace overcast::dsp
