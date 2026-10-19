#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

namespace overcast::dsp
{

// Coil: an amp-style spring tank. Each spring is a feedback loop around a long chain of
// first-order allpass filters, which delays low frequencies more than high ones and
// produces the characteristic "chirp" and drip (after Valimaki, Parker and Abel,
// "Parametric spring reverberation effect", JAES 2010).
// param1 = number of springs (1 to 3), param2 = dwell (drive into the tank).
class Coil final : public ReverbMachine
{
public:
    static constexpr std::size_t kMaxStages = 192;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        // Scale the chain length with sample rate so the dispersion lasts the same time.
        stages = std::clamp<std::size_t> (static_cast<std::size_t> (std::lround (48.0 * fs / 48000.0)), 24, kMaxStages);
        for (std::size_t s = 0; s < 3; ++s)
            springs[s].line.prepare (static_cast<std::size_t> (msToSamples (kSpringMs[s], fs)) + 8);
        for (auto& g : springGain) g.prepare (fs, 40.0f);
        loopGain.prepare (fs, 30.0f);
        damping.prepare (fs, 30.0f);
        drive.prepare (fs, 30.0f);
        inputGain.prepare (fs, 50.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        for (auto& s : springs)
        {
            s.line.reset();
            s.ap.fill (0.0f);
            s.damp.reset();
            s.dc.reset();
            s.fb = 0.0f;
        }
        sat.reset();
        for (auto* sm : { &loopGain, &damping, &drive, &inputGain })
            sm->snapTo (sm->getTarget());
        for (auto& g : springGain) g.snapTo (g.getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        const int count = 1 + static_cast<int> (std::clamp (p.param1, 0.0f, 1.0f) * 2.999f);
        for (std::size_t s = 0; s < 3; ++s)
            springGain[s].setTarget (static_cast<int> (s) < count ? 1.0f / std::sqrt (static_cast<float> (count)) : 0.0f);

        // Real spring tanks decay in 1 to 6 s; honour the knob within a sensible range.
        const float rt = std::clamp (p.decaySeconds, 0.3f, 12.0f);
        loopGain.setTarget (p.freeze ? 1.0f : decayGain (msToSamples (kSpringMs[1], fs), rt, fs));
        damping.setTarget (p.freeze ? 0.0f : OnePoleLowpass::coefficientFor (4200.0f, fs));
        drive.setTarget (1.0f + 7.0f * std::clamp (p.param2, 0.0f, 1.0f));
        inputGain.setTarget (p.freeze ? 0.0f : 1.0f);
        lfoDepth = p.mod;
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        constexpr float a = 0.62f;
        for (int i = 0; i < n; ++i)
        {
            const float g = loopGain.next(), damp = damping.next(), dr = drive.next(), in = inputGain.next();
            // Dwell: drive the tank through antialiased saturation, then compensate the level.
            const float x = in * sat.process (0.5f * (inL[i] + inR[i]) * dr) / std::sqrt (dr);
            float outL = 0.0f, outR = 0.0f;
            for (std::size_t s = 0; s < 3; ++s)
            {
                auto& sp = springs[s];
                const float sg = springGain[s].next();
                float v = x + g * sp.fb;
                for (std::size_t k = 0; k < stages; ++k)
                {
                    const float y = a * v + sp.ap[k];
                    sp.ap[k] = v - a * y;
                    v = y;
                }
                const float d = sp.line.read (static_cast<std::size_t> (msToSamples (kSpringMs[s], fs)));
                sp.line.push (v);
                sp.fb = sp.dc.process (sp.damp.process (d, damp));
                outL += sg * kPanL[s] * d;
                outR += sg * kPanR[s] * d;
            }
            wetL[i] = outL;
            wetR[i] = outR;
        }
    }

private:
    // DC blocker; a highpass at a few Hz keeps the loop free of offsets. Scaled by
    // (1 + R) / 2 so its gain never exceeds one (the plain form gains slightly at Nyquist).
    struct DcBlocker
    {
        static constexpr float R = 0.9995f;
        float x1 = 0.0f, y1 = 0.0f;
        void reset() { x1 = y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = 0.5f * (1.0f + R) * (x - x1) + R * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    struct Spring
    {
        DelayLine line;
        std::array<float, kMaxStages> ap {};
        OnePoleLowpass damp;
        DcBlocker dc;
        float fb = 0.0f;
    };

    static constexpr std::array<float, 3> kSpringMs { 33.1f, 41.3f, 47.9f };
    static constexpr std::array<float, 3> kPanL { 0.9f, 0.35f, 0.65f };
    static constexpr std::array<float, 3> kPanR { 0.35f, 0.9f, 0.65f };

    double fs = 48000.0;
    std::size_t stages = 48;
    std::array<Spring, 3> springs;
    std::array<Smoother, 3> springGain;
    Smoother loopGain, damping, drive, inputGain;
    AdaaTanh sat;
    float lfoDepth = 0.0f;
    MachineParams params;
};

} // namespace overcast::dsp
