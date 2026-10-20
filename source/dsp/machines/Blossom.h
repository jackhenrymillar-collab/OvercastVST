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
            l->prepare (static_cast<std::size_t> (msToSamples (345.0f, fs)) + 8);
        diffL.prepare (fs, { 2.3f, 3.7f, 5.3f, 6.7f, 8.1f, 9.7f });
        diffR.prepare (fs, { 2.9f, 3.3f, 4.9f, 7.1f, 8.7f, 9.3f });
        length.prepare (fs, 120.0f);
        feedback.prepare (fs, 30.0f);
        inputGain.prepare (fs, 50.0f);
        wobbleL.prepare (fs); wobbleR.prepare (fs);
        wobbleL.setFrequency (0.23f); wobbleR.setFrequency (0.31f); wobbleR.setPhase (0.4f);
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
            // The loop length drifts by up to 1.5 ms so its repeats never settle into a
            // fixed comb: with a fixed length the high-feedback settings rang at one pitch.
            const float drift = msToSamples (1.5f, fs);
            const float backL = loopL.readFractional (len + drift * (1.0f + wobbleL.next()));
            const float backR = loopR.readFractional (len * 1.07f + drift * (1.0f + wobbleR.next()));
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
    Diffuser<6> diffL, diffR; // short stages (under 10 ms): dense without colouring the sound
    OnePoleLowpass dampL, dampR;
    Smoother length, feedback, inputGain;
    Lfo wobbleL, wobbleR;
    MachineParams params;
};

} // namespace overcast::dsp
