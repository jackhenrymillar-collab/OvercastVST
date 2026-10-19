#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

#include <cstdint>

namespace overcast::dsp
{

// Gate: nonlinear reverbs in the style of 1980s studio units. Diffused input feeds a
// bank of 96 delay taps spread over the gate time, whose gains trace an envelope:
// gate (flat, then a sharp cut), reverse (rising ramp) or swell (rise and fall).
// Decay sets the gate time (80 ms to 1.5 s). param1 morphs the envelope shape,
// param2 = diffusion. Freeze mutes the input and lets the pattern ring out.
class Gate final : public ReverbMachine
{
public:
    static constexpr std::size_t kTaps = 96;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        diffInL.prepare (fs, { 3.1f, 4.7f, 7.9f, 11.3f });
        diffInR.prepare (fs, { 3.7f, 5.3f, 7.1f, 12.1f });
        diffOutL.prepare (fs, { 2.3f, 3.9f });
        diffOutR.prepare (fs, { 2.9f, 4.3f });
        tapsL.prepare (fs, 1600.0f);
        tapsR.prepare (fs, 1600.0f);
        diffusion.prepare (fs, 40.0f);
        inputGain.prepare (fs, 30.0f);
        lastGateMs = lastShape = -1.0f; // force the taps to be laid out for this sample rate
        setParams (params);
        reset();
    }

    void reset() override
    {
        diffInL.reset(); diffInR.reset(); diffOutL.reset(); diffOutR.reset();
        tapsL.reset(); tapsR.reset(); tapsL.snap(); tapsR.snap();
        diffusion.snapTo (diffusion.getTarget()); inputGain.snapTo (inputGain.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        diffusion.setTarget (0.35f + 0.27f * std::clamp (p.param2, 0.0f, 1.0f));
        inputGain.setTarget (p.freeze ? 0.0f : 1.0f);

        const float gateMs = 1000.0f * std::clamp (p.decaySeconds, 0.08f, 1.5f);
        const float shape = std::clamp (p.param1, 0.0f, 1.0f) * 2.0f;
        if (std::abs (gateMs - lastGateMs) < 1.0e-3f && std::abs (shape - lastShape) < 1.0e-6f)
            return;
        lastGateMs = gateMs;
        lastShape = shape;

        std::array<float, kTaps> env {};
        float energy = 0.0f;
        for (std::size_t k = 0; k < kTaps; ++k)
        {
            const float t = (static_cast<float> (k) + 0.5f) / static_cast<float> (kTaps);
            const float gate = t < 0.88f ? 1.0f : std::max (0.0f, 1.0f - (t - 0.88f) / 0.12f);
            const float reverse = t;
            const float swell = std::sin (std::numbers::pi_v<float> * t);
            const float e = shape <= 1.0f ? gate + shape * (reverse - gate) : reverse + (shape - 1.0f) * (swell - reverse);
            env[k] = e;
            energy += e * e;
        }
        const float norm = 1.0f / std::sqrt (std::max (energy, 1.0e-6f));
        // Evenly spaced taps are a comb filter that rings at the spacing's pitch, and any
        // repeating pattern of offsets (even a golden-ratio one) makes the taps line up
        // again further along. So each tap is moved by up to 45% of the spacing, with a
        // random sign, from a fixed-seed generator: the pattern is the same every time
        // but never repeats.
        const float spacing = gateMs / static_cast<float> (kTaps);
        std::uint32_t seedL = 0x9e3779b9u, seedR = 0x7f4a7c15u;
        const auto next = [] (std::uint32_t& s)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5; // xorshift32
            return static_cast<float> (s >> 8) / 16777216.0f;
        };
        for (std::size_t k = 0; k < kTaps; ++k)
        {
            const float base = spacing * (static_cast<float> (k) + 0.5f);
            const float jitterL = 0.9f * (next (seedL) - 0.5f);
            const float jitterR = 0.9f * (next (seedR) - 0.5f);
            const float signL = next (seedL) < 0.5f ? -1.0f : 1.0f;
            const float signR = next (seedR) < 0.5f ? -1.0f : 1.0f;
            tapsL.setTap (k, std::max (0.1f, base + jitterL * spacing), signL * env[k] * norm);
            tapsR.setTap (k, std::max (0.1f, base + jitterR * spacing), signR * env[k] * norm);
        }
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float c = diffusion.next(), g = inputGain.next();
            const float l = tapsL.process (diffInL.process (inL[i] * g, c));
            const float r = tapsR.process (diffInR.process (inR[i] * g, c));
            wetL[i] = 1.6f * diffOutL.process (l, 0.5f);
            wetR[i] = 1.6f * diffOutR.process (r, 0.5f);
        }
    }

private:
    double fs = 48000.0;
    Diffuser<4> diffInL, diffInR;
    Diffuser<2> diffOutL, diffOutR;
    TapDelay<kTaps> tapsL, tapsR;
    Smoother diffusion, inputGain;
    float lastGateMs = -1.0f, lastShape = -1.0f;
    MachineParams params;
};

} // namespace overcast::dsp
