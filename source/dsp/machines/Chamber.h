#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Chamber: small to medium rooms. Early reflections from a scaled tap pattern feed a
// short, dense FDN. param1 = room size, param2 = early/late balance.
//
// Short delay lines on their own ring at individual pitches (a "boxy", metallic tail),
// so the late network uses 16 lines with an allpass in each feedback path, and its
// delays shrink less than the early reflections do as the room gets smaller.
class Chamber final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        erL.prepare (fs, 50.0f * kMaxScale);
        erR.prepare (fs, 50.0f * kMaxScale);
        diffL.prepare (fs, { 2.3f, 3.7f });
        diffR.prepare (fs, { 2.9f, 3.3f });
        fdn.prepare (fs, kLate.back() * 1.4f * kMaxLateScale);
        erGain.prepare (fs, 30.0f);
        lateGain.prepare (fs, 30.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        erL.reset(); erR.reset(); erL.snap(); erR.snap();
        diffL.reset(); diffR.reset(); fdn.reset();
        absorbL.reset(); absorbR.reset();
        erGain.snapTo (erGain.getTarget()); lateGain.snapTo (lateGain.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const float scale = 0.4f + (kMaxScale - 0.4f) * std::clamp (p.param1, 0.0f, 1.0f);
        float g = 0.85f;
        for (std::size_t i = 0; i < 12; ++i)
        {
            const float sign = (i % 3 == 1) ? -1.0f : 1.0f;
            erL.setTap (i, kTapsL[i] * scale, sign * g);
            erR.setTap (i, kTapsR[i] * scale, -sign * g);
            g *= 0.86f;
        }
        const float lateScale = 0.8f + (kMaxLateScale - 0.8f) * std::clamp (p.param1, 0.0f, 1.0f);
        std::array<float, 16> ms {}, ap {};
        for (std::size_t i = 0; i < 16; ++i)
        {
            ms[i] = kLate[i] * 1.4f * lateScale;
            ap[i] = kLoopAllpass[i] * lateScale;
        }
        fdn.setDelays (ms);
        fdn.setLoopAllpasses (ap, 0.55f);
        DecaySettings d;
        d.rtMid = p.decaySeconds;
        d.rtLow = p.decaySeconds * 1.1f;
        d.rtHigh = p.decaySeconds * 0.45f;
        d.highHz = 5000.0f;
        fdn.setDecay (d);
        fdn.setModulation (p.mod * 0.5f);
        fdn.setFreeze (p.freeze);
        const float bal = std::clamp (p.param2, 0.0f, 1.0f);
        erGain.setTarget (p.freeze ? 0.0f : 1.0f - 0.8f * bal);
        lateGain.setTarget (0.35f + 0.65f * bal);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const float absorb = OnePoleLowpass::coefficientFor (7000.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float el = absorbL.process (erL.process (inL[i]), absorb);
            const float er = absorbR.process (erR.process (inR[i]), absorb);
            float tl, tr;
            fdn.process (diffL.process (inL[i], 0.6f), diffR.process (inR[i], 0.6f), tl, tr);
            const float eg = erGain.next(), lg = lateGain.next();
            wetL[i] = eg * el + lg * tl;
            wetR[i] = eg * er + lg * tr;
        }
    }

private:
    static constexpr float kMaxScale = 2.0f;
    static constexpr std::array<float, 12> kTapsL { 3.1f, 5.9f, 8.3f, 11.7f, 14.3f, 17.9f, 21.1f, 24.7f, 29.3f, 33.1f, 38.9f, 44.3f };
    static constexpr std::array<float, 12> kTapsR { 4.3f, 6.7f, 9.9f, 12.1f, 15.7f, 19.3f, 22.9f, 26.3f, 31.1f, 35.9f, 41.3f, 47.1f };
    static constexpr float kMaxLateScale = 1.5f;
    static constexpr std::array<float, 16> kLate { 7.3f, 8.3f, 9.1f, 10.1f, 11.3f, 12.1f, 13.7f, 14.9f,
                                                   15.9f, 17.1f, 17.9f, 19.7f, 21.1f, 22.3f, 23.3f, 25.1f };
    static constexpr std::array<float, 16> kLoopAllpass { 1.13f, 1.71f, 2.37f, 2.93f, 1.39f, 2.11f, 2.69f, 3.31f,
                                                          1.27f, 1.93f, 2.53f, 3.13f, 1.51f, 2.23f, 2.83f, 3.47f };

    double fs = 48000.0;
    TapDelay<12> erL, erR;
    OnePoleLowpass absorbL, absorbR;
    Diffuser<2> diffL, diffR;
    Fdn<16> fdn;
    Smoother erGain, lateGain;
    MachineParams params;
};

} // namespace overcast::dsp
