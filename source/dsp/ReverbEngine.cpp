#include "ReverbEngine.h"

namespace overcast::dsp
{

namespace
{
    constexpr float kToneSplitHz = 1000.0f;
    constexpr float kToneMaxDb = 6.0f;
    constexpr float kSilence = 1.0e-5f;     // -100 dBFS: a spilling tail below this is finished
    constexpr float kSilenceHoldSeconds = 0.3f;
}

ReverbEngine::ReverbEngine()
{
    for (int i = 0; i < kNumMachines; ++i)
        machines[static_cast<std::size_t> (i)] = createMachine (static_cast<MachineId> (i));
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
    maxBlock = std::max (1, maxBlockSize);

    for (auto& m : machines)
        m->prepare (sampleRate, maxBlock);

    const auto maxPre = static_cast<std::size_t> (kMaxPreDelayMs * 0.001 * sampleRate) + 8;
    preDelayL.prepare (maxPre);
    preDelayR.prepare (maxPre);

    for (auto* v : { &preL, &preR, &wetL, &wetR, &oldL, &oldR, &zeros })
        v->assign (static_cast<std::size_t> (maxBlock), 0.0f);

    preDelaySamples.prepare (sampleRate, 80.0f);
    for (auto* s : { &dryGain, &wetGain, &toneLow, &toneHigh })
        s->prepare (sampleRate, 20.0f);
    lowCutHz.prepare (sampleRate, 30.0f);
    highCutHz.prepare (sampleRate, 30.0f);
    fadeStep = 1.0f / (kSwitchFadeMs * 0.001f * static_cast<float> (sampleRate));

    prepared = true;
    active = std::clamp (params.machine, 0, kNumMachines - 1);
    reset();
}

void ReverbEngine::reset()
{
    for (auto& m : machines)
        m->reset();
    previous = -1;
    fadeIn = 1.0f;
    preDelayL.reset(); preDelayR.reset();
    lowCutL.reset(); lowCutR.reset(); highCutL.reset(); highCutR.reset();
    toneSplitL.reset(); toneSplitR.reset();

    setParams (params);
    for (auto* s : { &preDelaySamples, &dryGain, &wetGain, &toneLow, &toneHigh, &lowCutHz, &highCutHz })
        s->snapTo (s->getTarget());
    appliedLowCutHz = appliedHighCutHz = -1.0f;
}

void ReverbEngine::switchTo (int machine)
{
    if (machine == active)
        return;

    // Only one outgoing machine at a time: a third switch cuts the oldest tail.
    if (previous >= 0)
        machines[static_cast<std::size_t> (previous)]->reset();

    previous = active;
    previousSpills = params.spillover;
    fadeOut = 1.0f;
    quietSamples = 0;

    // The outgoing machine stops being fed new input; never leave it frozen.
    MachineParams old = params.machineParams;
    old.freeze = false;
    machines[static_cast<std::size_t> (previous)]->setParams (old);

    active = machine;
    machines[static_cast<std::size_t> (active)]->reset();
    fadeIn = 0.0f;
}

void ReverbEngine::setParams (const EngineParams& p)
{
    const int requested = std::clamp (p.machine, 0, kNumMachines - 1);
    params = p;
    if (prepared)
        switchTo (requested);
    else
        active = requested;

    auto& machine = *machines[static_cast<std::size_t> (active)];
    MachineParams mp = p.machineParams;
    mp.preDelayMs = p.preDelayMs;
    machine.setParams (mp);

    const float preMs = machine.consumesPreDelay() ? 0.0f : std::clamp (p.preDelayMs, 0.0f, kMaxPreDelayMs);
    preDelaySamples.setTarget (std::max (preMs * 0.001f * static_cast<float> (fs), 2.0f));

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
        const auto un = static_cast<std::size_t> (n);
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
        for (std::size_t i = 0; i < un; ++i)
        {
            const float d = preDelaySamples.next();
            preDelayL.push (L[i]);
            preDelayR.push (R[i]);
            preL[i] = preDelayL.readFractional (d);
            preR[i] = preDelayR.readFractional (d);
        }

        auto& current = *machines[static_cast<std::size_t> (active)];
        current.process (preL.data(), preR.data(), wetL.data(), wetR.data(), n);
        const float* dryEnv = current.dryEnvelope();

        // Level-match the machine, and fade it in over 30 ms after a switch.
        const float trimActive = kMachineTrim[active];
        for (std::size_t i = 0; i < un; ++i)
        {
            fadeIn = std::min (1.0f, fadeIn + fadeStep);
            wetL[i] *= trimActive * fadeIn;
            wetR[i] *= trimActive * fadeIn;
        }

        // The outgoing machine: fed silence, it either rings out (spillover) or fades in 30 ms.
        if (previous >= 0)
        {
            auto& old = *machines[static_cast<std::size_t> (previous)];
            old.process (zeros.data(), zeros.data(), oldL.data(), oldR.data(), n);
            const float trim = kMachineTrim[previous];
            float peak = 0.0f;
            for (std::size_t i = 0; i < un; ++i)
            {
                if (! previousSpills)
                    fadeOut = std::max (0.0f, fadeOut - fadeStep);
                wetL[i] += fadeOut * trim * oldL[i];
                wetR[i] += fadeOut * trim * oldR[i];
                peak = std::max ({ peak, std::abs (oldL[i]), std::abs (oldR[i]) });
            }
            quietSamples = peak * fadeOut < kSilence ? quietSamples + n : 0;
            if (fadeOut <= 0.0f || quietSamples > static_cast<int> (kSilenceHoldSeconds * fs))
            {
                old.reset();
                previous = -1;
            }
        }

        const float toneA = OnePoleLowpass::coefficientFor (kToneSplitHz, fs);
        for (std::size_t i = 0; i < un; ++i)
        {
            const float tl = toneLow.next(), th = toneHigh.next();
            float wl = wetL[i];
            float wr = wetR[i];

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
            const float de = dryEnv != nullptr ? dryEnv[i] : 1.0f;
            L[i] = L[i] * (dg * de) + wl * wg;
            R[i] = R[i] * (dg * de) + wr * wg;
        }

        offset += n;
    }
}

} // namespace overcast::dsp
