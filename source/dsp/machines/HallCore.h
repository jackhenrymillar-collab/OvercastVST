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
        std::array<float, 8> ms {};
        const float s = std::clamp (size, 0.3f, kMaxSize);
        for (std::size_t i = 0; i < 8; ++i)
            ms[i] = kBase[i] * s;
        fdn.setDelays (ms);
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
    static constexpr std::array<float, 8> kBase { 29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 61.7f, 67.1f, 79.3f };
    static constexpr float kDiffusion = 0.7f;

    double fs = 48000.0;
    Diffuser<4> diffL, diffR;
    Fdn<8> fdn;
};

} // namespace overcast::dsp
