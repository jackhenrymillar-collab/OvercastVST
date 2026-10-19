#pragma once

#include "../Shared.h"

namespace overcast::dsp
{

// Input diffusion into an 8-line FDN: the shared "hall" engine behind Arena, Blossom,
// Rise, Choir and Tape Echo.
class HallCore
{
public:
    static constexpr float kMaxSize = 2.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        diffL.prepare (fs, { 4.7f, 3.6f, 12.7f, 9.3f });
        diffR.prepare (fs, { 5.3f, 3.1f, 11.9f, 9.9f });
        fdn.prepare (fs, kBase.back() * kMaxSize);
        configure (3.0f, 1.0f, 0.5f, 0.5f, 1.0f, false);
        reset();
    }

    void reset() { diffL.reset(); diffR.reset(); fdn.reset(); }

    void configure (float decaySeconds, float lowMult, float highMult, float mod, float size, bool freeze)
    {
        std::array<float, kLines> ms {}, ap {};
        const float s = std::clamp (size, 0.3f, kMaxSize);
        for (std::size_t i = 0; i < kLines; ++i)
        {
            ms[i] = kBase[i] * s;
            ap[i] = kLoopAllpass[i] * s;
        }
        fdn.setDelays (ms);
        fdn.setLoopAllpasses (ap, kLoopAllpassGain);
        DecaySettings d;
        d.rtMid = decaySeconds;
        d.rtLow = decaySeconds * lowMult;
        d.rtHigh = decaySeconds * highMult;
        fdn.setDecay (d);
        fdn.setModulation (mod);
        fdn.setFreeze (freeze);
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float l = diffL.process (inL, kDiffusion);
        const float r = diffR.process (inR, kDiffusion);
        fdn.process (l, r, outL, outR);
    }

private:
    // Sixteen lines over the same 30 to 80 ms span as before, with an allpass in each
    // loop. Eight lines left gaps between echoes that you could hear as a flutter and as
    // ringing pitches once Mod was turned down; twice the lines and the allpasses fill
    // the gaps so the tail is smooth with or without modulation.
    static constexpr std::size_t kLines = 16;
    static constexpr std::array<float, kLines> kBase { 29.7f, 31.9f, 34.3f, 37.1f, 39.7f, 41.1f, 43.7f, 47.3f,
                                                       51.1f, 53.9f, 57.7f, 61.7f, 64.3f, 67.1f, 73.7f, 79.3f };
    static constexpr std::array<float, kLines> kLoopAllpass { 1.31f, 3.67f, 2.03f, 2.89f, 1.57f, 3.23f, 2.41f, 1.13f,
                                                              3.89f, 1.79f, 2.63f, 3.41f, 1.97f, 2.21f, 3.07f, 1.43f };
    static constexpr float kLoopAllpassGain = 0.5f;
    static constexpr float kDiffusion = 0.7f;

    double fs = 48000.0;
    Diffuser<4> diffL, diffR;
    Fdn<kLines> fdn;
};

} // namespace overcast::dsp
