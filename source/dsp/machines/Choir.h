#pragma once

#include "../ReverbMachine.h"
#include "HallCore.h"

namespace overcast::dsp
{

// Choir: a hall followed by vocal formant filters, so the tail sings a vowel.
// Formant frequencies are average adult values from Peterson and Barney (1952).
// param1 = vowel (A, E, I, O, U, morphing smoothly), param2 = formant mix.
class Choir final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        core.prepare (fs);
        vowel.prepare (fs, 80.0f);
        mix.prepare (fs, 30.0f);
        vibratoL.prepare (fs); vibratoR.prepare (fs);
        vibratoL.setFrequency (4.7f); vibratoR.setFrequency (5.3f); vibratoR.setPhase (0.3f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        core.reset();
        for (auto& f : formantsL) f.reset();
        for (auto& f : formantsR) f.reset();
        vowel.snapTo (vowel.getTarget()); mix.snapTo (mix.getTarget());
        counter = 0;
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        core.configure (p.decaySeconds, 1.0f, 0.6f, p.mod, 1.2f, p.freeze);
        vowel.setTarget (std::clamp (p.param1, 0.0f, 1.0f) * 4.0f);
        mix.setTarget (std::clamp (p.param2, 0.0f, 1.0f));
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float v = vowel.next();
            const float vl = vibratoL.next(), vr = vibratoR.next();
            if ((counter++ & 31) == 0)
                updateFormants (v, vl, vr);

            float l, r;
            core.process (inL[i], inR[i], l, r);
            float fl = 0.0f, fr = 0.0f;
            for (std::size_t k = 0; k < 3; ++k)
            {
                fl += kGains[k] * formantsL[k].process (l);
                fr += kGains[k] * formantsR[k].process (r);
            }
            const float m = mix.next();
            wetL[i] = (1.0f - m) * l + m * kFormantMakeup * fl;
            wetR[i] = (1.0f - m) * r + m * kFormantMakeup * fr;
        }
    }

private:
    void updateFormants (float v, float vibL, float vibR)
    {
        const auto idx = std::min<std::size_t> (static_cast<std::size_t> (v), 3);
        const float t = v - static_cast<float> (idx);
        for (std::size_t k = 0; k < 3; ++k)
        {
            const float hz = kVowels[idx][k] + t * (kVowels[idx + 1][k] - kVowels[idx][k]);
            formantsL[k].setBandpass (hz * (1.0f + 0.012f * vibL), kQ[k], fs);
            formantsR[k].setBandpass (hz * (1.0f + 0.012f * vibR), kQ[k], fs);
        }
    }

    // F1, F2, F3 in Hz for A (father), E (bed), I (beet), O (bought), U (boot).
    static constexpr float kVowels[5][3] = { { 730.0f, 1090.0f, 2440.0f },
                                             { 530.0f, 1840.0f, 2480.0f },
                                             { 270.0f, 2290.0f, 3010.0f },
                                             { 570.0f, 840.0f, 2410.0f },
                                             { 300.0f, 870.0f, 2240.0f } };
    static constexpr std::array<float, 3> kGains { 1.0f, 0.6f, 0.35f };
    static constexpr std::array<float, 3> kQ { 7.0f, 11.0f, 15.0f };
    static constexpr float kFormantMakeup = 3.0f;

    double fs = 48000.0;
    HallCore core;
    std::array<Biquad, 3> formantsL, formantsR;
    Smoother vowel, mix;
    Lfo vibratoL, vibratoR;
    unsigned counter = 0;
    MachineParams params;
};

} // namespace overcast::dsp
