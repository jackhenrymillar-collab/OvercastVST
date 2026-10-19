#pragma once

// Shared building blocks used by several machines: a feedback delay network,
// diffusers, early reflections, antialiased saturation, envelope following and
// pitch shifting. Nothing here allocates after prepare().

#include "Blocks.h"

#include <array>

namespace overcast::dsp
{

inline float msToSamples (float ms, double fs) noexcept
{
    return ms * 0.001f * static_cast<float> (fs);
}

// Decay gain for one pass through a delay of `delaySamples` so the loop reaches -60 dB in rt60.
inline float decayGain (float delaySamples, float rt60Seconds, double fs) noexcept
{
    return std::pow (10.0f, -3.0f * delaySamples / (static_cast<float> (fs) * std::max (0.05f, rt60Seconds)));
}

// In-place fast Walsh-Hadamard transform, normalised so it is orthogonal (lossless).
template <std::size_t N>
inline void hadamard (std::array<float, N>& x) noexcept
{
    static_assert ((N & (N - 1)) == 0, "N must be a power of two");
    for (std::size_t h = 1; h < N; h <<= 1)
        for (std::size_t i = 0; i < N; i += h << 1)
            for (std::size_t j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    const float norm = 1.0f / std::sqrt (static_cast<float> (N));
    for (auto& v : x)
        v *= norm;
}

// Frequency-dependent decay inside a feedback loop: separate gains for low, mid and high
// bands, split by two complementary one-pole crossovers (the bands sum back to the input).
class ThreeBandDecay
{
public:
    void reset() { lpLow.reset(); lpHigh.reset(); }

    float process (float x, float aLow, float aHigh, float gLow, float gMid, float gHigh) noexcept
    {
        const float low = lpLow.process (x, aLow);
        const float belowHigh = lpHigh.process (x, aHigh);
        return gLow * low + gMid * (belowHigh - low) + gHigh * (x - belowHigh);
    }

private:
    OnePoleLowpass lpLow, lpHigh;
};

struct DecaySettings
{
    float rtLow = 3.0f, rtMid = 3.0f, rtHigh = 1.5f; // seconds
    float lowHz = 250.0f, highHz = 4000.0f;          // crossover points
};

// N-line feedback delay network with a Hadamard mixing matrix, three-band decay,
// smoothed (glide-free) delay changes, gentle delay modulation and a lossless freeze.
template <std::size_t N>
class Fdn
{
public:
    void prepare (double sampleRate, float maxDelayMs)
    {
        fs = sampleRate;
        maxDelay = msToSamples (maxDelayMs, fs) + msToSamples (kMaxModMs, fs) + 8.0f;
        for (std::size_t i = 0; i < N; ++i)
        {
            lines[i].prepare (static_cast<std::size_t> (maxDelay) + 8);
            delays[i].prepare (fs, 60.0f);
            gLow[i].prepare (fs, 30.0f);
            gMid[i].prepare (fs, 30.0f);
            gHigh[i].prepare (fs, 30.0f);
            lfos[i].prepare (fs);
            lfos[i].setPhase (static_cast<float> (i) / static_cast<float> (N));
            lfos[i].setFrequency (0.11f + 0.07f * static_cast<float> (i));
            // Alternating input/output signs decorrelate the channels.
            signs[i] = ((i * 7 + i / 2) % 3 == 0) ? -1.0f : 1.0f;
        }
        for (std::size_t i = 0; i < N; ++i)
        {
            loopAllpass[i].prepare (static_cast<std::size_t> (msToSamples (kMaxLoopAllpassMs, fs)) + 2);
            loopAllpassDelay[i].prepare (fs, 60.0f);
        }
        inputGain.prepare (fs, 50.0f);
        modDepth.prepare (fs, 50.0f);
        reset();
    }

    void reset()
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            lines[i].reset();
            loopAllpass[i].reset();
            loopAllpassDelay[i].snapTo (loopAllpassDelay[i].getTarget());
            decay[i].reset();
            delays[i].snapTo (delays[i].getTarget());
            gLow[i].snapTo (gLow[i].getTarget());
            gMid[i].snapTo (gMid[i].getTarget());
            gHigh[i].snapTo (gHigh[i].getTarget());
        }
        inputGain.snapTo (inputGain.getTarget());
        modDepth.snapTo (modDepth.getTarget());
    }

    // Delay lengths in milliseconds; changes glide smoothly.
    void setDelays (const std::array<float, N>& ms)
    {
        for (std::size_t i = 0; i < N; ++i)
            delays[i].setTarget (std::clamp (msToSamples (ms[i], fs), 4.0f, maxDelay - msToSamples (kMaxModMs, fs) - 4.0f));
        updateGains();
    }

    void setDecay (const DecaySettings& d)
    {
        decaySettings = d;
        aLow = OnePoleLowpass::coefficientFor (d.lowHz, fs);
        aHigh = OnePoleLowpass::coefficientFor (d.highHz, fs);
        updateGains();
    }

    // Optional allpass inside each feedback path. Short networks (small rooms) otherwise
    // have too few echo paths and ring at individual pitches; an allpass per line multiplies
    // the echo density and spreads the modes without changing the tone or the decay.
    // Gain 0 turns it off. Delays are in milliseconds, up to kMaxLoopAllpassMs.
    void setLoopAllpasses (const std::array<float, N>& ms, float gain)
    {
        const float maxSamples = msToSamples (kMaxLoopAllpassMs, fs);
        for (std::size_t i = 0; i < N; ++i)
        {
            const float target = std::clamp (std::round (msToSamples (ms[i], fs)), 1.0f, maxSamples);
            if (loopAllpassDelay[i].getCurrent() < 1.0f) loopAllpassDelay[i].snapTo (target); // first use
            else                                         loopAllpassDelay[i].setTarget (target);
        }
        loopAllpassGain = std::clamp (gain, 0.0f, 0.75f);
        updateGains();
    }

    // depth 0..1 maps to 0..kMaxModMs of delay excursion.
    void setModulation (float depth) { modDepth.setTarget (std::clamp (depth, 0.0f, 1.0f) * msToSamples (kMaxModMs, fs)); }

    void setFreeze (bool f)
    {
        frozen = f;
        inputGain.setTarget (f ? 0.0f : 1.0f);
        updateGains();
    }

    // Optional per-line hook inside the loop (used by Halo for pitch shifting).
    template <typename LineHook>
    void process (float inL, float inR, float& outL, float& outR, LineHook&& hook) noexcept
    {
        const float depth = modDepth.next();
        const float in = inputGain.next();
        std::array<float, N> y {};
        float sumL = 0.0f, sumR = 0.0f;
        for (std::size_t i = 0; i < N; ++i)
        {
            const float d = delays[i].next() + depth * (0.5f + 0.5f * lfos[i].next()) + 2.0f;
            y[i] = lines[i].readFractional (d);
            if (i % 2 == 0) sumL += signs[i] * y[i];
            else            sumR += signs[i] * y[i];
            float v = decay[i].process (y[i], aLow, aHigh, gLow[i].next(), gMid[i].next(), gHigh[i].next());
            if (loopAllpassGain > 0.0f)
            {
                // The length glides when the size changes (a jump would click); once
                // settled it is a whole number of samples and needs no interpolation.
                auto& apd = loopAllpassDelay[i];
                if (apd.isSettled())
                    v = loopAllpass[i].process (v, static_cast<std::size_t> (apd.getTarget()), loopAllpassGain);
                else
                    v = loopAllpass[i].processModulated (v, apd.next(), loopAllpassGain);
            }
            y[i] = hook (i, v);
        }
        hadamard (y);
        for (std::size_t i = 0; i < N; ++i)
            lines[i].push (y[i] + in * signs[i] * (i % 2 == 0 ? inL : inR));

        constexpr float outNorm = 1.0f / static_cast<float> (N / 2);
        outL = sumL * std::sqrt (outNorm);
        outR = sumR * std::sqrt (outNorm);
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        process (inL, inR, outL, outR, [] (std::size_t, float v) { return v; });
    }

    // Energy loss of the loop is bounded by the decay gains, so the network is stable for
    // any setting where every gain is <= 1 (the mixing matrix is orthogonal).
    static constexpr float kMaxModMs = 3.0f;
    static constexpr float kMaxLoopAllpassMs = 8.0f;

private:
    void updateGains()
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            // An allpass delays the signal by its length on average, so it counts toward the loop time.
            const float d = delays[i].getTarget()
                          + (loopAllpassGain > 0.0f ? loopAllpassDelay[i].getTarget() : 0.0f);
            if (frozen)
            {
                gLow[i].setTarget (1.0f);
                gMid[i].setTarget (1.0f);
                gHigh[i].setTarget (1.0f);
            }
            else
            {
                gLow[i].setTarget (decayGain (d, decaySettings.rtLow, fs));
                gMid[i].setTarget (decayGain (d, decaySettings.rtMid, fs));
                gHigh[i].setTarget (decayGain (d, decaySettings.rtHigh, fs));
            }
        }
    }

    double fs = 48000.0;
    float maxDelay = 0.0f;
    std::array<DelayLine, N> lines;
    std::array<Allpass, N> loopAllpass;
    std::array<Smoother, N> loopAllpassDelay;
    float loopAllpassGain = 0.0f;
    std::array<ThreeBandDecay, N> decay;
    std::array<Smoother, N> delays, gLow, gMid, gHigh;
    std::array<Lfo, N> lfos;
    std::array<float, N> signs {};
    Smoother inputGain, modDepth;
    DecaySettings decaySettings;
    float aLow = 0.0f, aHigh = 0.0f;
    bool frozen = false;
};

// Chain of allpass diffusers for one channel, optionally modulated ("smear").
template <std::size_t N>
class Diffuser
{
public:
    void prepare (double sampleRate, const std::array<float, N>& delaysMs, float maxStretch = 1.0f)
    {
        fs = sampleRate;
        for (std::size_t i = 0; i < N; ++i)
        {
            baseDelays[i] = msToSamples (delaysMs[i], fs);
            allpasses[i].prepare (static_cast<std::size_t> (baseDelays[i] * maxStretch + msToSamples (2.0f, fs)) + 8);
            lfos[i].prepare (fs);
            lfos[i].setFrequency (0.3f + 0.13f * static_cast<float> (i));
            lfos[i].setPhase (0.37f * static_cast<float> (i));
        }
        maxScale = maxStretch;
    }

    void reset() { for (auto& a : allpasses) a.reset(); }

    // scale stretches every delay (0.25..maxScale), smearMs adds modulation.
    float process (float x, float coefficient, float scale = 1.0f, float smearMs = 0.0f) noexcept
    {
        const float smear = msToSamples (smearMs, fs);
        const float s = std::clamp (scale, 0.25f, maxScale);
        for (std::size_t i = 0; i < N; ++i)
        {
            const float d = std::max (3.0f, baseDelays[i] * s + (smear > 0.0f ? smear * (0.5f + 0.5f * lfos[i].next()) : 0.0f));
            x = allpasses[i].processModulated (x, d, coefficient);
        }
        return x;
    }

private:
    double fs = 48000.0;
    float maxScale = 1.0f;
    std::array<float, N> baseDelays {};
    std::array<Allpass, N> allpasses;
    std::array<Lfo, N> lfos;
};

// Multi-tap early reflections read from one delay line. Tap times glide smoothly when
// they change; once settled, taps sit on whole samples and use a cheap direct read.
template <std::size_t MaxTaps>
class TapDelay
{
public:
    void prepare (double sampleRate, float maxMs)
    {
        fs = sampleRate;
        maxSamples = msToSamples (maxMs, fs);
        line.prepare (static_cast<std::size_t> (maxSamples) + 8);
        for (auto& t : times) t.prepare (fs, 40.0f);
        for (auto& g : gains) g.prepare (fs, 40.0f);
    }

    void reset()
    {
        line.reset();
        snap();
    }

    void setTap (std::size_t i, float ms, float gain)
    {
        // Whole-sample targets: identical sound, and a settled tap needs no interpolation.
        times[i].setTarget (std::round (std::clamp (msToSamples (ms, fs), 2.0f, std::max (2.0f, maxSamples))));
        gains[i].setTarget (gain);
        moving = true;
    }

    void snap()
    {
        for (auto& t : times) t.snapTo (t.getTarget());
        for (auto& g : gains) g.snapTo (g.getTarget());
        settle();
    }

    void setNumTaps (std::size_t n) { numTaps = std::min (n, MaxTaps); }

    float process (float x) noexcept
    {
        line.push (x);
        float y = 0.0f;
        if (! moving)
        {
            for (std::size_t i = 0; i < numTaps; ++i)
                y += fixedGains[i] * line.read (fixedTimes[i]);
            return y;
        }

        bool stillMoving = false;
        for (std::size_t i = 0; i < numTaps; ++i)
        {
            const float t = times[i].next();
            const float g = gains[i].next();
            stillMoving = stillMoving || ! times[i].isSettled() || ! gains[i].isSettled();
            y += g * line.readFractional (t);
        }
        if (! stillMoving)
            settle();
        return y;
    }

private:
    void settle()
    {
        for (std::size_t i = 0; i < MaxTaps; ++i)
        {
            fixedTimes[i] = static_cast<std::size_t> (std::max (2.0f, times[i].getTarget()));
            fixedGains[i] = gains[i].getTarget();
        }
        moving = false;
    }

    double fs = 48000.0;
    float maxSamples = 0.0f;
    DelayLine line;
    std::array<Smoother, MaxTaps> times, gains;
    std::array<std::size_t, MaxTaps> fixedTimes {};
    std::array<float, MaxTaps> fixedGains {};
    std::size_t numTaps = MaxTaps;
    bool moving = false;
};

// tanh saturation with first-order antiderivative antialiasing (Parker, Zavalishin,
// Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time
// convolution", DAFx 2016). Much less aliasing than plain tanh at no oversampling cost.
class AdaaTanh
{
public:
    void reset() { x1 = 0.0f; f1 = 0.0f; }

    float process (float x) noexcept
    {
        const float f = antiderivative (x);
        const float dx = x - x1;
        float y;
        if (std::abs (dx) > 1.0e-4f)
            y = (f - f1) / dx;
        else
            y = std::tanh (0.5f * (x + x1));
        x1 = x;
        f1 = f;
        return y;
    }

private:
    // log(cosh(x)), computed without overflow.
    static float antiderivative (float x) noexcept
    {
        const float ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0f * ax)) - std::numbers::ln2_v<float>;
    }

    float x1 = 0.0f, f1 = 0.0f;
};

// Peak envelope follower with separate attack and release.
class EnvelopeFollower
{
public:
    void prepare (double sampleRate, float attackMs, float releaseMs)
    {
        att = std::exp (-1.0f / (attackMs * 0.001f * static_cast<float> (sampleRate)));
        rel = std::exp (-1.0f / (releaseMs * 0.001f * static_cast<float> (sampleRate)));
    }

    void reset() { env = 0.0f; }

    float process (float x) noexcept
    {
        const float a = std::abs (x);
        env = a > env ? a + att * (env - a) : a + rel * (env - a);
        return env;
    }

private:
    float att = 0.0f, rel = 0.0f, env = 0.0f;
};

// Two-head granular pitch shifter with complementary sin^2 windows (the gains always sum
// to one). Placed inside a feedback loop it keeps loop gain at or below one.
class PitchShifter
{
public:
    void prepare (double sampleRate, float windowMs)
    {
        window = msToSamples (windowMs, sampleRate);
        line.prepare (static_cast<std::size_t> (window) + 16);
    }

    void reset() { line.reset(); phase = 0.0f; }

    void setRatio (float r) noexcept { step = (1.0f - r) / window; }

    float process (float x) noexcept
    {
        line.push (x);
        phase += step;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
        const float w1 = std::sin (std::numbers::pi_v<float> * phase);
        const float w2 = std::sin (std::numbers::pi_v<float> * p2);
        return w1 * w1 * line.readFractional (3.0f + phase * window)
             + w2 * w2 * line.readFractional (3.0f + p2 * window);
    }

private:
    DelayLine line;
    float window = 2400.0f;
    float phase = 0.0f;
    float step = 0.0f;
};

} // namespace overcast::dsp
