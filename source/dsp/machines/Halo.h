#pragma once

#include "../ReverbMachine.h"
#include "../Shared.h"

#include <algorithm>
#include <cstdint>

namespace overcast::dsp
{

// Multi-voice granular pitch shifter for the shimmer loop. Four read heads sweep through a
// short delay line a quarter of a grain apart, each faded in and out by a Hann window. Each
// head starts every grain from a slightly different, random point (up to a few ms back), so
// the voices don't line up and the shimmer sounds like a smooth, wide choir instead of a
// buzzy single copy.
//
// Overlap-add gain: four Hann windows a quarter apart have squared gains that always sum to
// 1.5, so dividing by sqrt(1.5) keeps the level of the (unrelated) voices steady with no
// flutter at the grain rate.
class GrainShifter
{
public:
    static constexpr int kVoices = 4;

    void prepare (double sampleRate, float grainMs, float jitterMs, std::uint32_t seed)
    {
        grain = msToSamples (grainMs, sampleRate);
        jitterMax = msToSamples (jitterMs, sampleRate);
        line.prepare (static_cast<std::size_t> (grain + jitterMax) + 16);
        state = seed * 2654435761u + 1u;
        reset();
    }

    void reset()
    {
        line.reset();
        for (int v = 0; v < kVoices; ++v)
        {
            phase[v] = static_cast<float> (v) / kVoices;
            jitter[v] = jitterMax * unit();
        }
    }

    // ratio > 1 shifts up (2 = an octave), < 1 shifts down.
    void setRatio (float r) noexcept { step = (1.0f - r) / grain; }

    float process (float x) noexcept
    {
        line.push (x);
        float y = 0.0f;
        for (int v = 0; v < kVoices; ++v)
        {
            float p = phase[v] + step;
            if (p >= 1.0f || p < 0.0f)
            {
                // The window is silent here, so the head can jump to a new start point.
                p -= std::floor (p);
                jitter[v] = jitterMax * unit();
            }
            phase[v] = p;
            const float s = std::sin (std::numbers::pi_v<float> * p);
            y += s * s * line.readFractional (3.0f + jitter[v] + p * grain);
        }
        return y * kOverlapNorm;
    }

private:
    float unit() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
    }

    static constexpr float kOverlapNorm = 0.81649658f; // 1 / sqrt(1.5)
    DelayLine line;
    float grain = 3360.0f, jitterMax = 240.0f, step = 0.0f;
    float phase[kVoices] {}, jitter[kVoices] {};
    std::uint32_t state = 1u;
};

// Halo: shimmer, after ShimmerVerb by Danial Kooshki (MIT). A Zita-Rev1 style reverb (the
// design by Fons Adriaensen, as in Julius O. Smith's Faust zita_rev1: eight long lines,
// each with a 13 to 32 ms allpass inside, a Hadamard mix and two-band decay) is fed back
// through a shimmer loop: the reverb's output is band-limited (250 Hz to 6 kHz), pitch
// shifted by one or two granular layers, safety-limited, and mixed back into the reverb's
// input. Each trip round the loop climbs another interval, so the tail rises in a wash of
// octaves rather than one line ringing up the scale.
// param1 = interval (-12, +7, +12, +12 with +7, +12 with +19 semitones),
// param2 = shimmer amount.
//
// Stability: the shimmer loop's gain is set from the reverb's own energy gain (which grows
// with Decay), so at full Shimmer each trip round the loop keeps at most 80% of the energy
// and the shimmer always dies away. A soft limiter in the loop caps anything louder.
class Halo final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int) override
    {
        fs = sampleRate;
        diffL.prepare (fs, { 1.3f, 2.1f, 3.1f, 4.3f, 6.1f, 8.3f });
        diffR.prepare (fs, { 1.5f, 2.3f, 2.9f, 4.7f, 5.9f, 8.9f });
        std::array<float, 8> lineMs {};
        for (std::size_t i = 0; i < 8; ++i)
            lineMs[i] = kLoopMs[i] - kAllpassMs[i];
        fdn.prepare (fs, *std::max_element (lineMs.begin(), lineMs.end()), 32.0f);
        fdn.setDelays (lineMs);
        fdn.setLoopAllpasses (kAllpassMs, 0.6f);
        for (std::size_t c = 0; c < 2; ++c)
        {
            layerA[c].prepare (fs, 70.0f, 5.0f, 11u + 7u * static_cast<std::uint32_t> (c));
            layerB[c].prepare (fs, 60.0f, 5.0f, 23u + 7u * static_cast<std::uint32_t> (c));
            hp1[c].setHighpass (250.0f, fs);
            hp2[c].setHighpass (250.0f, fs);
            lp1[c].setLowpass (kShimmerTopHz, fs);
            lp2[c].setLowpass (kShimmerTopHz, fs);
        }
        feedback.prepare (fs, 40.0f);
        gainA.prepare (fs, 40.0f);
        gainB.prepare (fs, 40.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        diffL.reset(); diffR.reset(); fdn.reset();
        for (std::size_t c = 0; c < 2; ++c)
        {
            layerA[c].reset(); layerB[c].reset();
            hp1[c].reset(); hp2[c].reset(); lp1[c].reset(); lp2[c].reset();
            loop[c] = 0.0f;
        }
        for (auto* sm : { &feedback, &gainA, &gainB })
            sm->snapTo (sm->getTarget());
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        // Frozen, the shimmer stops climbing so the held chord stays put and lossless.
        const float s = p.freeze ? 0.0f : std::clamp (p.param2, 0.0f, 1.0f);

        // The shimmer loop feeds energy back, which stretches the tail (by about 1.7x at
        // Shimmer 60). The reverb's own decay is shortened to match, so the whole tail
        // still lasts about as long as the Decay knob says.
        const float rt = p.decaySeconds / tailStretch (s);
        DecaySettings d;
        d.rtMid = rt;
        d.rtLow = rt;            // Zita's low decay, left equal to the mid decay
        d.rtHigh = rt * 0.5f;    // Zita's HF damping: the decay halves above it
        d.lowHz = 200.0f;
        d.highHz = 6000.0f;
        fdn.setDecay (d);
        fdn.setModulation (p.mod);
        // Extra movement in long tails. The shimmer loop keeps re-exciting the same low
        // modes, so this core needs more than the others: ringing score 9.4 at 1x, 5.9 at
        // 2x, 4.6 at 3x (sparse piano, 9 s, octave up).
        fdn.setTailMovement (3.0f);
        fdn.setFreeze (p.freeze);

        const auto& v = kVoicings[std::min<std::size_t> (static_cast<std::size_t> (std::clamp (p.param1, 0.0f, 1.0f) * 5.0f), 4)];
        for (std::size_t c = 0; c < 2; ++c)
        {
            layerA[c].setRatio (std::exp2 (v.semitonesA / 12.0f));
            layerB[c].setRatio (std::exp2 (v.semitonesB / 12.0f));
        }
        // Layer gains are power-normalised, so a two-layer voicing is no louder than one.
        const float norm = 1.0f / std::sqrt (v.gainA * v.gainA + v.gainB * v.gainB);
        gainA.setTarget (v.gainA * norm);
        gainB.setTarget (v.gainB * norm);

        feedback.setTarget (s * kMaxLoopGain / std::sqrt (energyGain (rt)));
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float fb = feedback.next(), ga = gainA.next(), gb = gainB.next();
            const float l = diffL.process (inL[i] + loop[0], 0.7f);
            const float r = diffR.process (inR[i] + loop[1], 0.7f);
            fdn.process (l, r, wetL[i], wetR[i]);

            // The shimmer loop: band-limit the tail, pitch shift it, limit it, feed it back.
            const float wet[2] = { wetL[i], wetR[i] };
            for (std::size_t c = 0; c < 2; ++c)
            {
                const float band = lp2[c].process (lp1[c].process (hp2[c].process (hp1[c].process (wet[c]))));
                const float shifted = ga * layerA[c].process (band) + (gb > 0.0f ? gb * layerB[c].process (band) : 0.0f);
                loop[c] = softLimit (fb * shifted);
            }
        }
    }

private:
    struct Voicing { float semitonesA, gainA, semitonesB, gainB; };
    static constexpr std::array<Voicing, 5> kVoicings { {
        { -12.0f, 1.0f, 0.0f, 0.0f },
        {   7.0f, 1.0f, 0.0f, 0.0f },
        {  12.0f, 1.0f, 0.0f, 0.0f },
        {  12.0f, 0.7f, 7.0f, 0.3f },   // ShimmerVerb's default: octave up with a fifth
        {  12.0f, 0.7f, 19.0f, 0.3f },
    } };

    // Zita-Rev1's loop times (each line plus its allpass) and allpass lengths, in ms.
    static constexpr std::array<float, 8> kLoopMs { 153.129f, 210.389f, 127.837f, 256.891f, 174.713f, 192.303f, 125.0f, 219.991f };
    static constexpr std::array<float, 8> kAllpassMs { 20.346f, 24.421f, 31.604f, 27.333f, 22.904f, 29.291f, 13.458f, 19.123f };

    // Top of the shimmer loop's band. Each trip doubles the pitch, so a lower top keeps the
    // climbing octaves from turning gritty (Valhalla's "Dark" mode does the same).
    static constexpr float kShimmerTopHz = 6000.0f;
    static constexpr float kMaxLoopGain = 0.8944f; // sqrt(0.8): at most 80% energy per trip

    // How much the shimmer loop lengthens the tail, measured at Shimmer 0, 30, 60 and 100%
    // (the same at 4, 9 and 12 s of Decay), with straight lines in between.
    static float tailStretch (float shimmer) noexcept
    {
        constexpr float x[] = { 0.0f, 0.3f, 0.6f, 1.0f }, y[] = { 1.0f, 1.16f, 1.66f, 1.85f };
        for (int k = 1; k < 4; ++k)
            if (shimmer <= x[k])
                return y[k - 1] + (y[k] - y[k - 1]) * (shimmer - x[k - 1]) / (x[k] - x[k - 1]);
        return y[3];
    }

    // Energy of the reverb's output for a unit impulse in, as a function of Decay. Measured
    // from this network (with the shimmer off) at 1 to 20 s: about 0.37 + 0.123 per second.
    static float energyGain (float decaySeconds) noexcept
    {
        return 0.37f + 0.123f * std::max (decaySeconds, 0.0f);
    }

    // Safety cap on the shimmer loop: untouched up to +6 dBFS (so loud playing comes back
    // clean, not squashed), then easing smoothly towards +12 dBFS so the loop can never run
    // away. A plain tanh here bent the shimmer from about -6 dBFS input.
    static float softLimit (float x) noexcept { return kLoopHeadroom * overcast::dsp::softLimit (x / kLoopHeadroom, 0.5f); }
    static constexpr float kLoopHeadroom = 4.0f;

    double fs = 48000.0;
    Diffuser<6> diffL, diffR; // short stages (under 10 ms): dense without colouring the sound
    Fdn<8> fdn;
    std::array<GrainShifter, 2> layerA, layerB;
    std::array<Biquad, 2> hp1, hp2, lp1, lp2;
    float loop[2] {};
    Smoother feedback, gainA, gainB;
    MachineParams params;
};

} // namespace overcast::dsp
