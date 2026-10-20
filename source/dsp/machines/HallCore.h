#pragma once

#include "../Shared.h"

namespace overcast::dsp
{

// Input diffusion into an 8-line FDN: the shared "hall" engine behind Arena, Blossom,
// Rise and Halo.
class HallCore
{
public:
    static constexpr float kMaxSize = 2.0f;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        diffL.prepare (fs, { 1.3f, 2.1f, 3.1f, 4.3f, 6.1f, 8.3f });
        diffR.prepare (fs, { 1.5f, 2.3f, 2.9f, 4.7f, 5.9f, 8.9f });
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
    // Prime sample counts at 48 kHz, with no two lines near a 2:1, 3:2 or 4:3 ratio.
    static constexpr std::array<float, kLines> kBase { 29.7292f, 31.8958f, 34.1042f, 37.1458f, 39.8542f, 41.1042f, 43.7292f, 47.2708f,
                                                       50.8542f, 53.9792f, 57.6458f, 61.2292f, 64.3542f, 67.1042f, 73.7292f, 79.2292f };
    static constexpr std::array<float, kLines> kLoopAllpass { 1.31f, 3.67f, 2.03f, 2.89f, 1.57f, 3.23f, 2.41f, 1.13f,
                                                              3.89f, 1.79f, 2.63f, 3.41f, 1.97f, 2.21f, 3.07f, 1.43f };
    static constexpr float kLoopAllpassGain = 0.5f;
    static constexpr float kDiffusion = 0.7f;

    double fs = 48000.0;
    Diffuser<6> diffL, diffR; // short stages (under 10 ms): dense without colouring the sound
    Fdn<kLines> fdn;
};

} // namespace overcast::dsp
