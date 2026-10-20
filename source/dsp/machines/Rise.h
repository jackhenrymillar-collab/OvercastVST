#pragma once

#include "../ReverbMachine.h"
#include "HallCore.h"

#include <vector>

namespace overcast::dsp
{

// Rise: an automatic volume swell. Each new note starts silent and fades in over the rise
// time, so the reverb blooms in behind the playing. param1 = rise time (50 ms to 3 s),
// param2 = mode: below 50% only the reverb swells, above 50% the dry signal swells too.
class Rise final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int maxBlockSize) override
    {
        fs = sampleRate;
        core.prepare (fs);
        // Extra long-tail movement from Rise's ringing score at 12 s of decay (6.2):
        // 1 + 0.5 x (score - 4).
        core.setTailMovement (2.1f);
        fast.prepare (fs, 0.5f, 25.0f);
        slow.prepare (fs, 40.0f, 400.0f);
        dryEnv.assign (static_cast<std::size_t> (maxBlockSize), 1.0f);
        setParams (params);
        reset();
    }

    void reset() override
    {
        core.reset(); fast.reset(); slow.reset();
        gain = 0.0f; quietSamples = 0; armed = true; falling = false;
        std::fill (dryEnv.begin(), dryEnv.end(), 1.0f);
    }

    void setParams (const MachineParams& p) override
    {
        params = p;
        core.configure (p.decaySeconds, 1.1f, 0.5f, p.mod, 1.3f, p.freeze);
        const float riseSeconds = 0.05f * std::pow (60.0f, std::clamp (p.param1, 0.0f, 1.0f));
        increment = 1.0f / (riseSeconds * static_cast<float> (fs));
        swellDry = p.param2 >= 0.5f;
    }

    void process (const float* inL, const float* inR, float* wetL, float* wetR, int n) override
    {
        constexpr float threshold = 0.0056f; // about -45 dBFS
        const int quietLimit = static_cast<int> (0.06 * fs);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (std::abs (inL[i]) + std::abs (inR[i]));
            const float f = fast.process (x);
            const float s = slow.process (x);

            if (f < threshold)
            {
                if (++quietSamples > quietLimit) { armed = true; gain = 0.0f; }
            }
            else
            {
                quietSamples = 0;
                // A new note while already playing: dip quickly, then swell again.
                if (! armed && f > 2.5f * s + threshold)
                    falling = true;
                armed = false;
            }

            if (falling)
            {
                gain *= 0.995f;
                if (gain < 0.01f) falling = false;
            }
            else if (! armed)
            {
                gain = std::min (1.0f, gain + increment);
            }

            // Smoothstep curve sounds like a natural hand-on-the-volume-pedal swell.
            const float shaped = gain * gain * (3.0f - 2.0f * gain);
            core.process (inL[i] * shaped, inR[i] * shaped, wetL[i], wetR[i]);
            if (static_cast<std::size_t> (i) < dryEnv.size())
                dryEnv[static_cast<std::size_t> (i)] = swellDry ? shaped : 1.0f;
        }
    }

    const float* dryEnvelope() const noexcept override { return swellDry ? dryEnv.data() : nullptr; }

private:
    double fs = 48000.0;
    HallCore core;
    EnvelopeFollower fast, slow;
    std::vector<float> dryEnv;
    float gain = 0.0f, increment = 0.001f;
    int quietSamples = 0;
    bool armed = true, falling = false, swellDry = false;
    MachineParams params;
};

} // namespace overcast::dsp
