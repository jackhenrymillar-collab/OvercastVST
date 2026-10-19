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
}

// The order must match dsp::MachineId and must never change (saved sessions store the index).
juce::StringArray machineNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace overcast
