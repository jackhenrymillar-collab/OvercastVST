#pragma once

#include "../ReverbMachine.h"
#include "HallCore.h"

namespace overcast::dsp
{

// Tape Echo: a three-head tape delay feeding a hall. The Pre-delay knob sets the spacing
// between heads (the echo time). Tape character comes from antialiased saturation on the
// record head, wow and flutter on playback, head bump, and treble loss on each repeat.
// param1 = head pattern (7 combinations of the three heads), param2 = feedback.
class TapeEcho final : public ReverbMachine
{
public:
    static constexpr float kMinSpacingMs = 40.0f, kMaxSpacingMs = 800.0f;

    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        core.prepare (fs);
        tape.prepare (static_cast<std::size_t> (msToSamples (3.0f * kMaxSpacingMs + 20.0f, fs)) + 8);
        spacing.prepare (fs, 250.0f); // slow glide, like changing tape speed
        feedback.prepare (fs, 30.0f);
        inputGain.prepare (fs, 50.0f);
        wow.prepare (fs); flutter.prepare (fs);
        wow.setFrequency (0.55f); flutter.setFrequency (7.3f);
        headBump.setPeak (110.0f, 1.0f, 3.0f, fs);
        lowCut.setHighpass (45.0f, fs);
        setParams (params);
        reset();
    }

    void reset() override
    {
        core.reset(); tape.reset(); sat.reset(); treble.reset(); headBump.reset(); lowCut.reset();
        for (auto* s : { &spacing, &feedback, &inputGain })
            s->snapTo (s->getTarget());
        headGains = headTargets;
        lastEcho = 0.0f;
    }

    bool consumesPreDelay() const noexcept override { return true; }

    void setParams (const MachineParams& p) override
    {
        params = p;
        core.configure (p.decaySeconds, 1.0f, 0.5f, 0.3f, 1.1f, p.freeze);
        spacing.setTarget (msToSamples (std::clamp (p.preDelayMs, kMinSpacingMs, kMaxSpacingMs), fs));
        frozen = p.freeze;
        feedback.setTarget (p.freeze ? 1.0f : 1.05f * std::clamp (p.param2, 0.0f, 1.0f));
        inputGain.setTarget (p.freeze ? 0.0f : 1.0f);
        const auto pattern = std::min (static_cast<int> (std::clamp (p.param1, 0.0f, 1.0f) * 7.0f), 6);
        static constexpr int kPatterns[7] = { 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111 };
        int active = 0;
        for (int h = 0; h < 3; ++h)
            active += (kPatterns[pattern] >> h) & 1;
        for (int h = 0; h < 3; ++h)
            headTargets[static_cast<std::size_t> (h)] = ((kPatterns[pattern] >> h) & 1) ? 1.0f / static_cast<float> (active) : 0.0f;
        wowDepth = msToSamples (1.6f * p.mod, fs);
        flutterDepth = msToSamples (0.12f * p.mod, fs);
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        const float trebleA = OnePoleLowpass::coefficientFor (5000.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const float sp = spacing.next(), fb = feedback.next(), in = inputGain.next();
            const float drift = wowDepth * (1.0f + wow.next()) + flutterDepth * (1.0f + flutter.next()) + 3.0f;

            // Record head: input plus the shaped repeats, through tape saturation. Frozen, the
            // loop skips the tone shaping so the held echoes neither fade nor build up.
            const float shaped = lowCut.process (headBump.process (treble.process (lastEcho, trebleA)));
            const float repeat = frozen ? lastEcho : shaped;
            tape.push (sat.process (0.5f * (inL[i] + inR[i]) * in + fb * repeat));

            float heads[3];
            float echo = 0.0f;
            for (std::size_t h = 0; h < 3; ++h)
            {
                headGains[h] += 0.001f * (headTargets[h] - headGains[h]);
                heads[h] = headGains[h] * tape.readFractional (sp * static_cast<float> (h + 1) + drift);
                echo += heads[h];
            }
            lastEcho = echo;

            // Spread the heads across the stereo field, then add the hall.
            const float el = 0.85f * heads[0] + 0.35f * heads[1] + 0.6f * heads[2];
            const float er = 0.35f * heads[0] + 0.85f * heads[1] + 0.6f * heads[2];
            float rl, rr;
            core.process (el, er, rl, rr);
            wetL[i] = 0.8f * el + 0.6f * rl;
            wetR[i] = 0.8f * er + 0.6f * rr;
        }
    }

private:
    double fs = 48000.0;
    HallCore core;
    DelayLine tape;
    AdaaTanh sat;
    OnePoleLowpass treble;
    Biquad headBump, lowCut;
    Lfo wow, flutter;
    Smoother spacing, feedback, inputGain;
    std::array<float, 3> headGains {}, headTargets { 1.0f, 0.0f, 0.0f };
    float wowDepth = 0.0f, flutterDepth = 0.0f, lastEcho = 0.0f;
    bool frozen = false;
    MachineParams params;
};

} // namespace overcast::dsp
