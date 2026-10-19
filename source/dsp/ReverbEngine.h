#pragma once

#include "Blocks.h"
#include "Machines.h"
#include "ReverbMachine.h"

#include <array>
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
    int machine = static_cast<int> (MachineId::sheet);
    MachineParams machineParams;
    float preDelayMs = 20.0f;     // 0 to 500 ms
    float mix = 0.35f;            // 0 to 1
    float tone = 0.0f;            // -1 (dark) to +1 (bright)
    float lowCutHz = 20.0f;       // wet low cut
    float highCutHz = 20000.0f;   // wet high cut
    bool killDry = false;
    bool spillover = true;        // let the old machine's tail ring out when switching
    MixLaw mixLaw = MixLaw::pedal;
};

// Everything shared around the machines: machine switching (crossfade or spillover),
// pre-delay, wet filters, tone, limiter and mix. The dry path is bit-exact, zero latency.
class ReverbEngine
{
public:
    static constexpr float kMaxPreDelayMs = 500.0f;
    static constexpr float kSwitchFadeMs = 30.0f;

    ReverbEngine();

    // All allocation happens here, never on the audio thread.
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& params);

    // In-place stereo processing on two distinct buffers. Any block size is accepted.
    void process (float* left, float* right, int numSamples);

    static void mixGains (float mix, MixLaw law, bool killDry, float& dryGain, float& wetGain) noexcept;

    int activeMachine() const noexcept { return active; }
    bool isSpilling() const noexcept { return previous >= 0; }

private:
    void switchTo (int machine);

    double fs = 48000.0;
    int maxBlock = 0;

    std::array<std::unique_ptr<ReverbMachine>, kNumMachines> machines;
    int active = static_cast<int> (MachineId::sheet);
    int previous = -1;           // machine fading out or spilling over, -1 if none
    bool previousSpills = false; // true: rings out naturally; false: 30 ms fade
    float fadeIn = 1.0f, fadeOut = 0.0f, fadeStep = 0.0f;
    int quietSamples = 0;

    DelayLine preDelayL, preDelayR;
    std::vector<float> preL, preR, wetL, wetR, oldL, oldR, zeros;

    Biquad lowCutL, lowCutR, highCutL, highCutR;
    OnePoleLowpass toneSplitL, toneSplitR;

    Smoother preDelaySamples, dryGain, wetGain, toneLow, toneHigh, lowCutHz, highCutHz;
    float appliedLowCutHz = -1.0f, appliedHighCutHz = -1.0f;

    EngineParams params;
    bool prepared = false;
};

} // namespace overcast::dsp
