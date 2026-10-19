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
        diffL.prepare (sampleRate, { 8.9f, 12.7f, 16.3f, 21.1f, 26.9f, 31.3f, 37.9f, 43.1f });
        diffR.prepare (sampleRate, { 9.7f, 13.3f, 17.9f, 20.3f, 25.1f, 32.9f, 36.1f, 44.9f });
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
    static constexpr std::array<float, 16> kDelays { 61.3f, 67.9f, 71.3f, 77.9f, 83.3f, 89.9f, 94.1f, 101.3f,
                                                     107.9f, 113.3f, 119.9f, 127.1f, 133.7f, 139.1f, 146.3f, 153.7f };
    Diffuser<8> diffL, diffR;
    Fdn<16> fdn;
    Smoother diffusion, smear;
    MachineParams params;
};

} // namespace overcast::dsp
