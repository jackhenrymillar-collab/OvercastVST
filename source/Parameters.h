#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace overcast
{

// Parameter IDs are part of saved sessions and presets: never rename one.
namespace ParamID
{
    inline constexpr auto machine   = "machine";
    inline constexpr auto decay     = "decay";
    inline constexpr auto preDelay  = "predelay";
    inline constexpr auto mix       = "mix";
    inline constexpr auto tone      = "tone";
    inline constexpr auto mod       = "mod";
    inline constexpr auto param1    = "param1";
    inline constexpr auto param2    = "param2";
    inline constexpr auto lowCut    = "lowcut";
    inline constexpr auto highCut   = "highcut";
    inline constexpr auto freeze    = "freeze";
    inline constexpr auto spillover = "spillover";
    inline constexpr auto killDry   = "killdry";
    inline constexpr auto mixLaw    = "mixlaw";
    inline constexpr auto bypass    = "bypass";
}

// The order must match dsp::MachineId. Saved sessions store the index, so a change of
// order needs a migration (see dsp::migrateMachineIndex and kMachineLayout).
juce::StringArray machineNames();

// Version of the machine list stored with sessions and user presets. 1: the original
// twelve machines. 2: the six kept on 2026-10-20.
inline constexpr int kMachineLayout = 2;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace overcast
