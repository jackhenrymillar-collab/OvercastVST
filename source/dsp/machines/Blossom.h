#pragma once

#include "../ReverbMachine.h"
#include "HallCore.h"

namespace overcast::dsp
{

// Blossom: the tail grows denser and louder after the attack. A diffused feedback loop
// in front of the hall makes each note re-enter the reverb several times, each pass more
// diffuse. param1 = bloom length (40 to 300 ms per pass), param2 = loop feedback.
class Blossom final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        core.prepare (fs);
        for (auto* l : { &loopL, &loopR })
            l->prepare (static_cast<std::size_t> (msToSamples (340.0f, fs)) + 8);
        diffL.prepare (fs, { 7.1f, 11.3f, 17.9f, 23.1f });
        diffR.prepare (fs, { 7.9f, 10.7f, 18.7f, 22.3f });
        length.prepare (fs, 120.0f);
        feedback.prepare (fs, 30.0f);
        inputGain.prepare (fs, 50.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        core.reset(); loopL.reset(); loopR.reset(); diffL.reset(); diffR.reset();
        dampL.reset(); dampR.reset();
        length.snapTo (length.getTarget()); feedback.snapTo (feedback.getTarget()); inputGain.snapTo (inputGain.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        core.configure (p.decaySeconds, 1.1f, 0.5f, p.mod, 1.3f, p.freeze);
        length.setTarget (msToSamples (40.0f + 260.0f * std::clamp (p.param1, 0.0f, 1.0f), fs));
        feedback.setTarget (0.85f * std::clamp (p.param2, 0.0f, 1.0f));
        inputGain.setTarget (p.freeze ? 0.0f : 1.0f);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const float damp = OnePoleLowpass::coefficientFor (6000.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float len = length.next(), fb = feedback.next(), in = inputGain.next();
            const float backL = loopL.readFractional (len);
            const float backR = loopR.readFractional (len * 1.07f);
            const float l = diffL.process (in * inL[i] + fb * dampL.process (backR, damp), 0.65f);
            const float r = diffR.process (in * inR[i] + fb * dampR.process (backL, damp), 0.65f);
            loopL.push (l);
            loopR.push (r);
            core.process (l, r, wetL[i], wetR[i]);
        }
    }

private:
    double fs = 48000.0;
    HallCore core;
    DelayLine loopL, loopR;
    Diffuser<4> diffL, diffR;
    OnePoleLowpass dampL, dampR;
    Smoother length, feedback, inputGain;
    MachineParams params;
};

} // namespace overcast::dsp
