#include "ReverbEngine.h"

namespace overcast::dsp
{

namespace
{
    constexpr float kToneSplitHz = 1000.0f;
    constexpr float kToneMaxDb = 6.0f;
}

void ReverbEngine::mixGains (float mix, MixLaw law, bool killDry, float& dry, float& wet) noexcept
{
    const float m = std::clamp (mix, 0.0f, 1.0f);
    if (law == MixLaw::equalPower)
    {
        dry = std::cos (m * std::numbers::pi_v<float> * 0.5f);
        wet = std::sin (m * std::numbers::pi_v<float> * 0.5f);
        if (m <= 0.0f) { dry = 1.0f; wet = 0.0f; }
    }
    else
    {
        dry = m <= 0.5f ? 1.0f : 2.0f * (1.0f - m);
        wet = m <= 0.5f ? 2.0f * m : 1.0f;
    }
    if (killDry)
        dry = 0.0f;
}

void ReverbEngine::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = maxBlockSize;

    plate->prepare (sampleRate, maxBlockSize);

    const auto maxPre = static_cast<std::size_t> (kMaxPreDelayMs * 0.001 * sampleRate) + 8;
    preDelayL.prepare (maxPre);
    preDelayR.prepare (maxPre);

    for (auto* v : { &preL, &preR, &wetL, &wetR })
        v->assign (static_cast<std::size_t> (maxBlockSize), 0.0f);

    preDelaySamples.prepare (sampleRate, 80.0f);
    for (auto* s : { &dryGain, &wetGain, &toneLow, &toneHigh })
        s->prepare (sampleRate, 20.0f);
    lowCutHz.prepare (sampleRate, 30.0f);
    highCutHz.prepare (sampleRate, 30.0f);

    reset();
}

void ReverbEngine::reset()
{
    plate->reset();
    preDelayL.reset(); preDelayR.reset();
    lowCutL.reset(); lowCutR.reset(); highCutL.reset(); highCutR.reset();
    toneSplitL.reset(); toneSplitR.reset();

    setParams (params);
    for (auto* s : { &preDelaySamples, &dryGain, &wetGain, &toneLow, &toneHigh, &lowCutHz, &highCutHz })
        s->snapTo (s->getTarget());
    appliedLowCutHz = appliedHighCutHz = -1.0f;
}

void ReverbEngine::setParams (const EngineParams& p)
{
    params = p;
    plate->setParams (p.machineParams);

    const float pre = std::clamp (p.preDelayMs, 0.0f, kMaxPreDelayMs) * 0.001f * static_cast<float> (fs);
    preDelaySamples.setTarget (std::max (pre, 2.0f));

    float d = 1.0f, w = 0.0f;
    mixGains (p.mix, p.mixLaw, p.killDry, d, w);
    dryGain.setTarget (d);
    wetGain.setTarget (w);

    const float toneDb = std::clamp (p.tone, -1.0f, 1.0f) * kToneMaxDb;
    toneHigh.setTarget (std::pow (10.0f, toneDb / 20.0f));
    toneLow.setTarget (std::pow (10.0f, -toneDb / 20.0f));

    lowCutHz.setTarget (std::clamp (p.lowCutHz, 10.0f, 2000.0f));
    highCutHz.setTarget (std::clamp (p.highCutHz, 500.0f, 22000.0f));
}

void ReverbEngine::process (float* left, float* right, int numSamples)
{
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = std::min (numSamples - offset, maxBlock);
        float* L = left + offset;
        float* R = right + offset;

        // Wet filter cutoffs are updated per chunk, smoothed across chunks.
        const float lc = lowCutHz.skip (n);
        const float hc = highCutHz.skip (n);
        if (lc != appliedLowCutHz)
        {
            lowCutL.setHighpass (lc, fs); lowCutR.setHighpass (lc, fs);
            appliedLowCutHz = lc;
        }
        if (hc != appliedHighCutHz)
        {
            highCutL.setLowpass (hc, fs); highCutR.setLowpass (hc, fs);
            appliedHighCutHz = hc;
        }

        // Pre-delay (fractional read so changes glide instead of clicking).
        for (int i = 0; i < n; ++i)
        {
            const float d = preDelaySamples.next();
            preDelayL.push (L[i]);
            preDelayR.push (R[i]);
            preL[static_cast<std::size_t> (i)] = preDelayL.readFractional (d);
            preR[static_cast<std::size_t> (i)] = preDelayR.readFractional (d);
        }

        activeMachine().process (preL.data(), preR.data(), wetL.data(), wetR.data(), n);

        const float toneA = OnePoleLowpass::coefficientFor (kToneSplitHz, fs);
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            const float tl = toneLow.next(), th = toneHigh.next();

            float wl = wetL[k];
            float wr = wetR[k];

            // Tilt EQ: split at 1 kHz, raise one side and lower the other.
            const float lowL = toneSplitL.process (wl, toneA);
            const float lowR = toneSplitR.process (wr, toneA);
            wl = lowL * tl + (wl - lowL) * th;
            wr = lowR * tl + (wr - lowR) * th;

            wl = highCutL.process (lowCutL.process (wl));
            wr = highCutR.process (lowCutR.process (wr));

            wl = softLimit (wl);
            wr = softLimit (wr);

            const float dg = dryGain.next();
            const float wg = wetGain.next();
            L[i] = L[i] * dg + wl * wg;
            R[i] = R[i] * dg + wr * wg;
        }

        offset += n;
    }
}

} // namespace overcast::dsp
