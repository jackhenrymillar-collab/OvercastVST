#pragma once

#include <juce_core/juce_core.h>

namespace overcast::ui
{

// What the panel shows for each machine. The order matches machineNames().
struct MachineInfo
{
    const char* name;
    const char* description;
    const char* param1Label;
    const char* param2Label;
};

const MachineInfo& machineInfo (int index);
int numMachines() noexcept;

// Knob labels that change with the machine.
juce::String decayLabel (int machine);
juce::String preDelayLabel (int machine);

// Value text under each knob, in the units the machine actually uses.
juce::String formatDecay (int machine, float seconds);
juce::String formatPreDelay (int machine, float ms);
juce::String formatPercent (float percent);
juce::String formatTone (float tone);
juce::String formatParam1 (int machine, float percent);
juce::String formatParam2 (int machine, float percent);
juce::String formatFrequency (float hz);

} // namespace overcast::ui
