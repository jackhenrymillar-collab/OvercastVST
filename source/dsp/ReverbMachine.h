#pragma once

#include <cstddef>

namespace overcast::dsp
{

// Parameters every machine receives. Values are already in physical units.
struct MachineParams
{
    float decaySeconds = 3.0f;   // RT60, 0.1 to 60 s
    float mod = 0.5f;            // 0 to 1, tail modulation depth
    float param1 = 0.0f;         // 0 to 1, machine-specific
    float param2 = 0.5f;         // 0 to 1, machine-specific
    bool freeze = false;         // hold the tail indefinitely
};

// Interface for one reverb algorithm. Produces the wet signal only.
// Implementations must not allocate, lock or do I/O in process().
class ReverbMachine
{
public:
    virtual ~ReverbMachine() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setParams (const MachineParams& params) = 0;

    // inL/inR may alias each other (mono input). wetL/wetR must not alias the inputs.
    virtual void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) = 0;
};

} // namespace overcast::dsp
