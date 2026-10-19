#pragma once

#include "Blocks.h"
#include "PlateReverb.h"
#include "ReverbMachine.h"

#include <memory>
#include <vector>

namespace overcast::dsp
{

enum class MixLaw
{
    equalPower, // dry and wet cross over at -3 dB each
    pedal       // dry stays at full level up to 50% mix, then fades
};

struct EngineParams
{
    int machine = 2;              // index into the machine list (2 = Sheet)
    MachineParams machineParams;
    float preDelayMs = 20.0f;     // 0 to 500 ms
    float mix = 0.35f;            // 0 to 1
    float tone = 0.0f;            // -1 (dark) to +1 (bright)
    float lowCutHz = 20.0f;       // wet low cut
    float highCutHz = 20000.0f;   // wet high cut
    bool killDry = false;
    MixLaw mixLaw = MixLaw::pedal;
};

// Everything shared around a machine: pre-delay, wet filters, tone, limiter and mix.
// The dry path is passed through bit-exact with zero latency.
class ReverbEngine
{
public:
    static constexpr float kMaxPreDelayMs = 500.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& params);

    // In-place stereo processing. For mono input pass the same channel for left and right
    // only if the caller has already copied it; left and right must be distinct buffers.
    void process (float* left, float* right, int numSamples);

    static void mixGains (float mix, MixLaw law, bool killDry, float& dryGain, float& wetGain) noexcept;

private:
    ReverbMachine& activeMachine() noexcept { return *plate; }

    double fs = 48000.0;
    int maxBlock = 0;

    std::unique_ptr<PlateReverb> plate = std::make_unique<PlateReverb>();

    DelayLine preDelayL, preDelayR;
    std::vector<float> preL, preR, wetL, wetR;

    Biquad lowCutL, lowCutR, highCutL, highCutR;
    OnePoleLowpass toneSplitL, toneSplitR;

    Smoother preDelaySamples, dryGain, wetGain, toneLow, toneHigh, lowCutHz, highCutHz;
    float appliedLowCutHz = -1.0f, appliedHighCutHz = -1.0f;

    EngineParams params;
};

} // namespace overcast::dsp
