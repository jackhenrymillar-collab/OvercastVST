#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Gate: nonlinear reverbs in the style of 1980s studio units. Diffused input feeds a
// bank of 48 delay taps spread over the gate time, whose gains trace an envelope:
// gate (flat, then a sharp cut), reverse (rising ramp) or swell (rise and fall).
// Decay sets the gate time (80 ms to 1.5 s). param1 morphs the envelope shape,
// param2 = diffusion. Freeze mutes the input and lets the pattern ring out.
class Gate final : public ReverbMachine
{
public:
    static constexpr std::size_t kTaps = 48;

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
        diffusion.setTarget (0.4f + 0.35f * std::clamp (p.param2, 0.0f, 1.0f));
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
        for (std::size_t k = 0; k < kTaps; ++k)
        {
            const float base = gateMs * (static_cast<float> (k) + 0.5f) / static_cast<float> (kTaps);
            const float sign = ((k * 5 + 1) % 3 == 0) ? -1.0f : 1.0f;
            tapsL.setTap (k, base + kJitter[k % 8] * gateMs / static_cast<float> (kTaps), sign * env[k] * norm);
            tapsR.setTap (k, base + kJitter[(k + 3) % 8] * gateMs / static_cast<float> (kTaps), -sign * env[k] * norm);
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
    static constexpr std::array<float, 8> kJitter { 0.11f, -0.23f, 0.31f, -0.07f, 0.19f, -0.29f, 0.03f, -0.17f };

    double fs = 48000.0;
    Diffuser<4> diffInL, diffInR;
    Diffuser<2> diffOutL, diffOutR;
    TapDelay<kTaps> tapsL, tapsR;
    Smoother diffusion, inputGain;
    float lastGateMs = -1.0f, lastShape = -1.0f;
    MachineParams params;
};

} // namespace overcast::dsp
