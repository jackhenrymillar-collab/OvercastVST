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
//
// Long decays: each spring is one loop, and past a few seconds of decay a single loop
// rings at its own row of pitches. From 3 s to 8 s of decay (and when frozen) the three
// springs are increasingly fed into each other through a slowly turning rotation (lossless,
// so the decay is unchanged), and the lows below 300 Hz are damped a little faster than
// the rest so the low mids don't hang on. Short decays are the classic tank.
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
        tailAmount.prepare (fs, 50.0f);
        lowGain.prepare (fs, 30.0f);
        lowCoefficient = OnePoleLowpass::coefficientFor (kLowHz, fs);
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
            s.low.reset();
            s.fb = 0.0f;
        }
        axisPhase = 0.0f;
        pairPhase = 0.3f;
        thirdPhase = 0.6f;
        tailAmount.snapTo (tailAmount.getTarget());
        lowGain.snapTo (lowGain.getTarget());
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
        const float loopSamples = msToSamples (kSpringMs[1], fs);
        loopGain.setTarget (p.freeze ? 1.0f : decayGain (loopSamples, rt, fs));
        const float t = p.freeze ? 1.0f : std::clamp ((p.decaySeconds - kTailStart) / (kTailFull - kTailStart), 0.0f, 1.0f);
        const float tail = t * t * (3.0f - 2.0f * t);
        tailAmount.setTarget (tail);
        // Extra loss for the lows, relative to the loop gain: they decay in 70% of the time at full tail.
        lowGain.setTarget (p.freeze ? 1.0f
                                    : decayGain (loopSamples, rt * (1.0f - 0.3f * tail), fs) / decayGain (loopSamples, rt, fs));
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
            float fb[3] = { springs[0].fb, springs[1].fb, springs[2].fb };
            const float tail = tailAmount.next(), lowG = lowGain.next();
            if (tail > 0.0f)
                mixSprings (fb, tail);
            for (std::size_t s = 0; s < 3; ++s)
            {
                auto& sp = springs[s];
                const float sg = springGain[s].next();
                const float lows = sp.low.process (fb[s], lowCoefficient);
                float v = x + g * (fb[s] + (lowG - 1.0f) * lows);
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
    // Three rotations, all lossless. The first turns the three springs about the axis they
    // share (1, 1, 1): at 180 degrees that is the Householder mix, which hands each spring
    // a third of the other two. The others turn springs 1 and 2, then 0 and 1, against each
    // other. All swing back and forth a few times a second, scaled by how long the tail is.
    void mixSprings (float (&v)[3], float tail) noexcept
    {
        axisPhase += kAxisHz / static_cast<float> (fs);
        pairPhase += kPairHz / static_cast<float> (fs);
        if (axisPhase >= 1.0f) axisPhase -= 1.0f;
        if (pairPhase >= 1.0f) pairPhase -= 1.0f;
        thirdPhase += kThirdHz / static_cast<float> (fs);
        if (thirdPhase >= 1.0f) thirdPhase -= 1.0f;

        // Rodrigues' formula about n = (1, 1, 1) / sqrt 3.
        const float phi = tail * 1.5707963f * (1.0f + std::sin (kTwoPi * axisPhase));
        const float c = std::cos (phi), s = std::sin (phi);
        const float k = (1.0f - c) * (v[0] + v[1] + v[2]) / 3.0f;
        const float sn = s / std::sqrt (3.0f);
        const float a = v[0], b = v[1], d = v[2];
        v[0] = c * a + k + sn * (b - d);
        v[1] = c * b + k + sn * (d - a);
        v[2] = c * d + k + sn * (a - b);

        const float psi = tail * 1.5707963f * std::sin (kTwoPi * pairPhase);
        const float pc = std::cos (psi), ps = std::sin (psi);
        const float u = v[1], w = v[2];
        v[1] = pc * u - ps * w;
        v[2] = ps * u + pc * w;

        const float chi = tail * 1.5707963f * std::sin (kTwoPi * thirdPhase);
        const float qc = std::cos (chi), qs = std::sin (chi);
        const float p = v[0], q = v[1];
        v[0] = qc * p - qs * q;
        v[1] = qs * p + qc * q;
    }

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
        OnePoleLowpass low;
        float fb = 0.0f;
    };

    static constexpr std::array<float, 3> kSpringMs { 33.1f, 41.3f, 47.9f };
    static constexpr float kTailStart = 3.0f, kTailFull = 8.0f; // seconds of decay
    static constexpr float kAxisHz = 3.0f, kPairHz = 4.1f, kThirdHz = 5.3f, kLowHz = 300.0f;
    static constexpr std::array<float, 3> kPanL { 0.9f, 0.35f, 0.65f };
    static constexpr std::array<float, 3> kPanR { 0.35f, 0.9f, 0.65f };

    double fs = 48000.0;
    std::size_t stages = 48;
    std::array<Spring, 3> springs;
    std::array<Smoother, 3> springGain;
    Smoother loopGain, damping, drive, inputGain, tailAmount, lowGain;
    float lowCoefficient = 0.0f, axisPhase = 0.0f, pairPhase = 0.0f, thirdPhase = 0.0f;
    AdaaTanh sat;
    float lfoDepth = 0.0f;
    MachineParams params;
};

} // namespace overcast::dsp
