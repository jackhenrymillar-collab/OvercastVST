#pragma once

// Small DSP building blocks shared by every reverb machine.
// Plain C++ with no framework dependencies, so they can be unit tested anywhere.
// Nothing here allocates after prepare().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace overcast::dsp
{

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

inline std::size_t nextPowerOfTwo (std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// Circular delay buffer. read(d) returns the sample pushed d samples ago (d >= 1).
class DelayLine
{
public:
    void prepare (std::size_t maxDelaySamples)
    {
        buffer.assign (nextPowerOfTwo (maxDelaySamples + 4), 0.0f);
        mask = buffer.size() - 1;
        writePos = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    float read (std::size_t delay) const noexcept
    {
        return buffer[(writePos - delay) & mask];
    }

    // Third-order Lagrange interpolation for fractional (modulated) delays.
    // Needs delay >= 2.
    float readFractional (float delay) const noexcept
    {
        const auto whole = static_cast<std::size_t> (delay);
        const float f = delay - static_cast<float> (whole);
        const float ym1 = read (whole - 1);
        const float y0  = read (whole);
        const float y1  = read (whole + 1);
        const float y2  = read (whole + 2);
        const float cm1 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
        const float c0  = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
        const float c1  = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
        const float c2  = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
        return cm1 * ym1 + c0 * y0 + c1 * y1 + c2 * y2;
    }

private:
    std::vector<float> buffer;
    std::size_t mask = 0;
    std::size_t writePos = 0;
};

// Schroeder allpass: H(z) = (z^-D - g) / (1 - g z^-D). Lossless for any |g| < 1.
class Allpass
{
public:
    void prepare (std::size_t maxDelaySamples) { line.prepare (maxDelaySamples + 8); }
    void reset() { line.reset(); }

    float process (float x, std::size_t delay, float g) noexcept
    {
        const float delayed = line.read (delay);
        const float w = x + g * delayed;
        line.push (w);
        return delayed - g * w;
    }

    float processModulated (float x, float delay, float g) noexcept
    {
        const float delayed = line.readFractional (delay);
        const float w = x + g * delayed;
        line.push (w);
        return delayed - g * w;
    }

    // Read the allpass's internal delay line (used for output taps).
    float tap (std::size_t delay) const noexcept { return line.read (delay); }

private:
    DelayLine line;
};

// One-pole lowpass: y = (1 - a) x + a y[n-1], a = exp(-2 pi fc / fs).
class OnePoleLowpass
{
public:
    static float coefficientFor (float cutoffHz, double sampleRate) noexcept
    {
        return std::exp (-kTwoPi * cutoffHz / static_cast<float> (sampleRate));
    }

    void reset() { state = 0.0f; }

    float process (float x, float a) noexcept
    {
        state = x + a * (state - x);
        return state;
    }

private:
    float state = 0.0f;
};

// Exponential parameter smoother. Converges to the target with time constant tau.
// State is kept in double so it reaches the target exactly instead of stalling a few
// float steps short (which would break bit-exact dry gain).
class Smoother
{
public:
    void prepare (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1.0 / (static_cast<double> (timeMs) * 0.001 * sampleRate));
    }

    void setTarget (float t) noexcept { target = t; }
    void snapTo (float t) noexcept { target = t; current = t; }
    float getTarget() const noexcept { return static_cast<float> (target); }
    float getCurrent() const noexcept { return static_cast<float> (current); }
    bool isSettled() const noexcept { return ! (current < target) && ! (current > target); }

    float next() noexcept
    {
        current = target + coeff * (current - target);
        if (std::abs (current - target) < 1.0e-9)
            current = target;
        return static_cast<float> (current);
    }

    // Advance several samples at once (for values updated per block).
    float skip (int numSamples) noexcept
    {
        current = target + std::pow (coeff, numSamples) * (current - target);
        if (std::abs (current - target) < 1.0e-9)
            current = target;
        return static_cast<float> (current);
    }

private:
    double coeff = 0.0;
    double current = 0.0;
    double target = 0.0;
};

// Sine LFO.
class Lfo
{
public:
    void prepare (double sampleRate) { fs = static_cast<float> (sampleRate); }
    void setFrequency (float hz) noexcept { increment = hz / fs; }
    void setPhase (float p) noexcept { phase = p; }

    float next() noexcept
    {
        const float out = std::sin (kTwoPi * phase);
        phase += increment;
        if (phase >= 1.0f)
            phase -= 1.0f;
        return out;
    }

private:
    float fs = 48000.0f;
    float phase = 0.0f;
    float increment = 0.0f;
};

// RBJ biquad, transposed direct form II. Used for the wet low/high cut.
class Biquad
{
public:
    void reset() { z1 = z2 = 0.0f; }

    void setHighpass (float hz, double sampleRate) { design (hz, sampleRate, true); }
    void setLowpass (float hz, double sampleRate) { design (hz, sampleRate, false); }

    // Constant 0 dB peak-gain bandpass (RBJ), used for formants.
    void setBandpass (float hz, float q, double sampleRate)
    {
        const double w0 = 2.0 * std::numbers::pi * std::min<double> (hz, 0.45 * sampleRate) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<float> (alpha / a0);
        b1 = 0.0f;
        b2 = static_cast<float> (-alpha / a0);
        a1 = static_cast<float> (-2.0 * std::cos (w0) / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    // Peaking EQ (RBJ), used for tape head bump.
    void setPeak (float hz, float q, float gainDb, double sampleRate)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * std::min<double> (hz, 0.45 * sampleRate) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cosw = std::cos (w0);
        const double a0 = 1.0 + alpha / A;
        b0 = static_cast<float> ((1.0 + alpha * A) / a0);
        b1 = static_cast<float> (-2.0 * cosw / a0);
        b2 = static_cast<float> ((1.0 - alpha * A) / a0);
        a1 = static_cast<float> (-2.0 * cosw / a0);
        a2 = static_cast<float> ((1.0 - alpha / A) / a0);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (float hz, double sampleRate, bool highpass)
    {
        const double w0 = 2.0 * std::numbers::pi * std::min<double> (hz, 0.45 * sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / std::numbers::sqrt2; // sin(w0) / (2Q), Q = 1/sqrt(2)
        const double a0 = 1.0 + alpha;
        double nb0, nb1, nb2;
        if (highpass)
        {
            nb0 = (1.0 + cosw) / 2.0;
            nb1 = -(1.0 + cosw);
            nb2 = nb0;
        }
        else
        {
            nb0 = (1.0 - cosw) / 2.0;
            nb1 = 1.0 - cosw;
            nb2 = nb0;
        }
        b0 = static_cast<float> (nb0 / a0);
        b1 = static_cast<float> (nb1 / a0);
        b2 = static_cast<float> (nb2 / a0);
        a1 = static_cast<float> (-2.0 * cosw / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// Transparent below the threshold, smoothly saturating above it. Never exceeds 1.0.
inline float softLimit (float x, float threshold = 0.9f) noexcept
{
    const float ax = std::abs (x);
    if (ax <= threshold)
        return x;
    const float range = 1.0f - threshold;
    const float y = threshold + range * std::tanh ((ax - threshold) / range);
    return std::copysign (y, x);
}

// Safety guard for signals inside a feedback loop. The loops are lossless at most, so
// this never acts in normal use: the loudest loop signal measured anywhere (+12 dBFS
// noise held into a frozen 60 s tail at 96 kHz) is about 24, well under the threshold
// of 64. Above that it saturates smoothly towards 128, so a runaway can never blow up.
// A value that is not a finite number is dropped to silence instead of spreading.
inline float loopGuard (float x) noexcept
{
    constexpr float kThreshold = 64.0f, kRange = 64.0f;
    const float ax = std::abs (x);
    if (ax <= kThreshold)
        return x;
    if (! (ax < 1.0e30f))
        return 0.0f;
    return std::copysign (kThreshold + kRange * std::tanh ((ax - kThreshold) / kRange), x);
}

} // namespace overcast::dsp
