#include "PlateReverb.h"

namespace overcast::dsp
{

namespace
{
    // Delay lengths from Dattorro's paper, in samples at his 29761 Hz reference rate. The two
    // longer input diffusers are shortened to under 10 ms (Dattorro: 379 and 277 samples).
    constexpr int kInputDiffusers[4] = { 142, 107, 293, 227 };
    // Slightly different lengths for the right input chain, so L and R decorrelate.
    constexpr int kInputDiffusersAlt[4] = { 151, 113, 281, 239 };
    constexpr float kInputDiffusion1 = 0.75f;
    constexpr float kInputDiffusion2 = 0.625f;

    constexpr int kModAllpassL = 672, kDelay1L = 4453, kAllpass2L = 1800, kDelay2L = 3720;
    constexpr int kModAllpassR = 908, kDelay1R = 4217, kAllpass2R = 2656, kDelay2R = 3163;
    constexpr float kDecayDiffusion1 = -0.70f;
    constexpr float kDecayDiffusion2 = 0.50f;
    constexpr int kModExcursion = 16;

    // Sum of all tank delays (one full trip round both halves).
    constexpr int kLoopLength = kModAllpassL + kDelay1L + kAllpass2L + kDelay2L
                              + kModAllpassR + kDelay1R + kAllpass2R + kDelay2R;
    constexpr double kReferenceRate = 29761.0;

    // Output taps, Dattorro table 2.
    constexpr int kTaps[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066,
                                353, 3627, 1228, 2673, 2111, 335, 121 };

    constexpr float kOutputGain = 0.6f;
    constexpr float kLfoRateHz = 0.9f;

    // Long tails: two slowly turning rotations mix the tank's halves (see process()).
    constexpr float kTailStart = 3.0f, kTailFull = 8.0f;   // seconds of decay
    constexpr float kMaxTurn = 1.5707963f;                  // 90 degrees
    constexpr float kJunctionHz = 0.31f, kMiddleHz = 0.43f;
}

std::size_t PlateReverb::scaled (int samplesAt29761) const noexcept
{
    return static_cast<std::size_t> (std::lround (samplesAt29761 * static_cast<double> (scale)));
}

void PlateReverb::prepare (double sampleRate, int /*maxBlockSize*/)
{
    fs = sampleRate;
    scale = static_cast<float> (sampleRate / kReferenceRate);

    for (int i = 0; i < 4; ++i)
    {
        inputDiffuserDelays[i] = scaled (kInputDiffusers[i]);
        inputDiffuserDelaysAlt[i] = scaled (kInputDiffusersAlt[i]);
        inputDiffusers[0][i].prepare (inputDiffuserDelays[i]);
        inputDiffusers[1][i].prepare (inputDiffuserDelaysAlt[i]);
    }

    modExcursion = static_cast<float> (kModExcursion) * scale;
    modDelayL = static_cast<float> (scaled (kModAllpassL));
    modDelayR = static_cast<float> (scaled (kModAllpassR));
    // Room for 2x the classic excursion plus interpolation points.
    const auto excursionRoom = static_cast<std::size_t> (2.0f * modExcursion) + 4;
    modAllpassL.prepare (static_cast<std::size_t> (modDelayL) + excursionRoom);
    modAllpassR.prepare (static_cast<std::size_t> (modDelayR) + excursionRoom);

    d1L = scaled (kDelay1L);   d1R = scaled (kDelay1R);
    ap2L = scaled (kAllpass2L); ap2R = scaled (kAllpass2R);
    d2L = scaled (kDelay2L);   d2R = scaled (kDelay2R);
    delay1L.prepare (d1L); delay1R.prepare (d1R);
    allpass2L.prepare (ap2L); allpass2R.prepare (ap2R);
    delay2L.prepare (d2L); delay2R.prepare (d2R);

    for (int i = 0; i < 14; ++i)
        taps[i] = std::max<std::size_t> (1, scaled (kTaps[i]));

    lfoL.prepare (sampleRate); lfoR.prepare (sampleRate);
    lfoL.setFrequency (kLfoRateHz); lfoR.setFrequency (kLfoRateHz * 1.07f);

    for (auto* s : { &decayGain, &damping, &lowCut, &modDepth, &tailTurn })
        s->prepare (sampleRate, 30.0f);
    inputGain.prepare (sampleRate, 50.0f);

    reset();
}

void PlateReverb::reset()
{
    for (auto& chain : inputDiffusers)
        for (auto& ap : chain)
            ap.reset();
    for (auto& b : bandwidth)
        b.reset();
    modAllpassL.reset(); modAllpassR.reset();
    delay1L.reset(); delay1R.reset();
    dampL.reset(); dampR.reset();
    lowL.reset(); lowR.reset();
    allpass2L.reset(); allpass2R.reset();
    delay2L.reset(); delay2R.reset();
    lfoL.setPhase (0.0f); lfoR.setPhase (0.25f);
    junctionPhase = 0.0f; middlePhase = 0.3f;
    feedbackL = feedbackR = 0.0f;

    setParams (current);
    decayGain.snapTo (decayGain.getTarget());
    damping.snapTo (damping.getTarget());
    lowCut.snapTo (lowCut.getTarget());
    modDepth.snapTo (modDepth.getTarget());
    inputGain.snapTo (inputGain.getTarget());
    tailTurn.snapTo (tailTurn.getTarget());
}

float PlateReverb::decayGainFor (float rt60Seconds) const noexcept
{
    // Four decay multipliers per trip round the tank. Solve g^(4 * rt60 / loopTime) = 10^-3.
    const double loopSeconds = kLoopLength / kReferenceRate;
    const double g = std::pow (10.0, -3.0 * loopSeconds / (4.0 * std::max (0.05f, rt60Seconds)));
    return static_cast<float> (std::min (g, 0.9999));
}

void PlateReverb::setParams (const MachineParams& p)
{
    current = p;
    const float fz = p.freeze ? 1.0f : 0.0f;

    // Freeze: lossless loop (gain 1, no damping, no low cut), input faded out.
    decayGain.setTarget (p.freeze ? 1.0f : decayGainFor (p.decaySeconds));

    // Damping: 16 kHz (bright) down to 1.5 kHz (dark), exponential across the knob.
    const float dampHz = 16000.0f * std::pow (1500.0f / 16000.0f, std::clamp (p.param2, 0.0f, 1.0f));
    damping.setTarget ((1.0f - fz) * OnePoleLowpass::coefficientFor (dampHz, fs));

    // Low end: 5 Hz (just a DC blocker) up to 400 Hz.
    const float lowHz = 5.0f * std::pow (80.0f, std::clamp (p.param1, 0.0f, 1.0f));
    lowCut.setTarget ((1.0f - fz) * (1.0f - OnePoleLowpass::coefficientFor (lowHz, fs)));

    modDepth.setTarget (std::clamp (p.mod, 0.0f, 1.0f) * 2.0f * modExcursion);
    inputGain.setTarget (1.0f - fz);

    const float t = p.freeze ? 1.0f : std::clamp ((p.decaySeconds - kTailStart) / (kTailFull - kTailStart), 0.0f, 1.0f);
    tailTurn.setTarget (kMaxTurn * t * t * (3.0f - 2.0f * t));
}

void PlateReverb::process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples)
{
    const float bwCoeff = OnePoleLowpass::coefficientFor (14000.0f, fs);

    for (int n = 0; n < numSamples; ++n)
    {
        const float g = decayGain.next();
        const float damp = damping.next();
        const float lowMix = lowCut.next();
        const float depth = modDepth.next();
        const float inGain = inputGain.next();
        const float turn = tailTurn.next();

        // Input diffusion, one chain per channel.
        float xL = bandwidth[0].process (inL[n] * inGain, bwCoeff);
        float xR = bandwidth[1].process (inR[n] * inGain, bwCoeff);
        xL = inputDiffusers[0][0].process (xL, inputDiffuserDelays[0], kInputDiffusion1);
        xL = inputDiffusers[0][1].process (xL, inputDiffuserDelays[1], kInputDiffusion1);
        xL = inputDiffusers[0][2].process (xL, inputDiffuserDelays[2], kInputDiffusion2);
        xL = inputDiffusers[0][3].process (xL, inputDiffuserDelays[3], kInputDiffusion2);
        xR = inputDiffusers[1][0].process (xR, inputDiffuserDelaysAlt[0], kInputDiffusion1);
        xR = inputDiffusers[1][1].process (xR, inputDiffuserDelaysAlt[1], kInputDiffusion1);
        xR = inputDiffusers[1][2].process (xR, inputDiffuserDelaysAlt[2], kInputDiffusion2);
        xR = inputDiffusers[1][3].process (xR, inputDiffuserDelaysAlt[3], kInputDiffusion2);

        // Left half of the tank (fed by the right half's output).
        float l = xL + feedbackR;
        l = modAllpassL.processModulated (l, modDelayL + depth * lfoL.next(), kDecayDiffusion1);
        const float l1 = delay1L.read (d1L);
        delay1L.push (l);
        float lt = dampL.process (l1, damp);
        lt -= lowMix * lowL.process (lt, 1.0f - lowMix);
        lt *= g;

        // Right half of the tank (fed by the left half's output).
        float r = xR + feedbackL;
        r = modAllpassR.processModulated (r, modDelayR + depth * lfoR.next(), kDecayDiffusion1);
        const float r1 = delay1R.read (d1R);
        delay1R.push (r);
        float rt = dampR.process (r1, damp);
        rt -= lowMix * lowR.process (rt, 1.0f - lowMix);
        rt *= g;
        // Long tails: the classic tank is one fixed loop, and past a few seconds of decay
        // it rings at separate pitches. Two rotations, one here between the halves and one
        // where they feed each other, turn back and forth by up to 90 degrees, so the loop
        // keeps changing shape and the pitches smear into a smooth tail. Rotations are
        // lossless and don't bend pitch. Short decays (turn = 0) are the classic plate.
        float middleCos = 1.0f, middleSin = 0.0f, junctionCos = 1.0f, junctionSin = 0.0f;
        if (turn > 0.0f)
        {
            middlePhase += kMiddleHz / static_cast<float> (fs);
            junctionPhase += kJunctionHz / static_cast<float> (fs);
            if (middlePhase >= 1.0f) middlePhase -= 1.0f;
            if (junctionPhase >= 1.0f) junctionPhase -= 1.0f;
            const float a = turn * std::sin (kTwoPi * middlePhase), b = turn * std::sin (kTwoPi * junctionPhase);
            middleCos = std::cos (a); middleSin = std::sin (a);
            junctionCos = std::cos (b); junctionSin = std::sin (b);
            const float l0 = lt, r0 = rt;
            lt = middleCos * l0 - middleSin * r0;
            rt = middleSin * l0 + middleCos * r0;
        }
        lt = allpass2L.process (lt, ap2L, kDecayDiffusion2);
        const float l2 = delay2L.read (d2L);
        delay2L.push (lt);
        rt = allpass2R.process (rt, ap2R, kDecayDiffusion2);
        const float r2 = delay2R.read (d2R);
        delay2R.push (rt);

        // feedbackL feeds the right half and feedbackR the left; at 90 degrees each half feeds itself.
        const float a = l2 * g, b = r2 * g;
        feedbackL = loopGuard (junctionCos * a + junctionSin * b);
        feedbackR = loopGuard (junctionCos * b - junctionSin * a);

        // Output taps (Dattorro table 2), left taps mostly from the right half and vice versa.
        float yL = delay1R.read (taps[0]) + delay1R.read (taps[1]) - allpass2R.tap (taps[2])
                 + delay2R.read (taps[3]) - delay1L.read (taps[4]) - allpass2L.tap (taps[5])
                 - delay2L.read (taps[6]);
        float yR = delay1L.read (taps[7]) + delay1L.read (taps[8]) - allpass2L.tap (taps[9])
                 + delay2L.read (taps[10]) - delay1R.read (taps[11]) - allpass2R.tap (taps[12])
                 - delay2R.read (taps[13]);

        wetL[n] = yL * kOutputGain;
        wetR[n] = yR * kOutputGain;
    }
}

} // namespace overcast::dsp
