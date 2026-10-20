#pragma once

#include "../Blocks.h"
#include "../ReverbMachine.h"

#include <array>

namespace overcast::dsp
{

// "Sheet": a studio plate based on the topology published by Jon Dattorro,
// "Effect Design, Part 1: Reverberator and Other Filters" (JAES, 1997),
// extended for true stereo input, sample-rate independent tuning,
// RT60-calibrated decay, in-loop low-end control and a lossless freeze.
//
// param1 = low end cut inside the tank (0 = full lows, 1 = lean, 400 Hz)
// param2 = damping (0 = bright, 1 = dark)
class PlateReverb final : public ReverbMachine
{
public:
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParams (const MachineParams& params) override;
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) override;

    // Exposed for tests: the decay gain applied at each of the four tank multipliers.
    float decayGainFor (float rt60Seconds) const noexcept;

private:
    std::size_t scaled (int samplesAt29761) const noexcept;

    double fs = 48000.0;
    float scale = 1.0f;

    // Input stage: one bandwidth filter and diffuser chain per input channel.
    std::array<OnePoleLowpass, 2> bandwidth;
    std::array<std::array<Allpass, 4>, 2> inputDiffusers;
    std::array<std::size_t, 4> inputDiffuserDelays {};
    std::array<std::size_t, 4> inputDiffuserDelaysAlt {};

    // Tank, two halves that feed each other.
    Allpass modAllpassL, modAllpassR;          // decay diffusion 1, modulated
    DelayLine delay1L, delay1R;
    OnePoleLowpass dampL, dampR;
    OnePoleLowpass lowL, lowR;                 // lowpass used to subtract lows (in-loop highpass)
    Allpass allpass2L, allpass2R;              // decay diffusion 2
    DelayLine delay2L, delay2R;

    float modDelayL = 0, modDelayR = 0;
    std::size_t d1L = 0, d1R = 0, ap2L = 0, ap2R = 0, d2L = 0, d2R = 0;
    float modExcursion = 0;

    Lfo lfoL, lfoR;

    // Output tap positions (scaled).
    std::array<std::size_t, 14> taps {};

    // Smoothed per-sample controls.
    Smoother decayGain, damping, lowCut, modDepth, inputGain, tailTurn;
    float turnRate = 1.0f;
    SweptAllpass smearL, smearR;
    float tail = 0.0f;

    float feedbackL = 0.0f, feedbackR = 0.0f;
    float junctionPhase = 0.0f, middlePhase = 0.0f;
    MachineParams current;
};

} // namespace overcast::dsp
