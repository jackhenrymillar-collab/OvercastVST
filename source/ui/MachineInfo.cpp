#include "MachineInfo.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace overcast::ui
{

namespace
{
    // Keep in step with the machines in source/dsp/machines and the README table.
    constexpr std::array<MachineInfo, 6> kMachines { {
        { "Blossom",   "A tail that slowly grows denser after the attack",      "Length",     "Feedback" },
        { "Haze",      "A huge, diffuse ambient wash with no clear echoes",     "Diffusion",  "Smear" },
        { "Aurora",    "Endless, slowly shifting wash for pads and ambient sound (after Cloud Seed)", "Size", "Diffusion" },
        { "Halo",      "Rising shimmer wash (after ShimmerVerb)",              "Interval",   "Shimmer" },
        { "Nebula",    "A velvet-noise wash that is fully dense from the first moment", "Darkness", "Softness" },
        { "Clear Night", "A colourless, evenly tuned hall with no ringing notes", "Size",     "Brightness" },
    } };

    enum Machine { blossom, haze, aurora, halo, nebula, clearNight };

    float unit (float percent) { return std::clamp (percent * 0.01f, 0.0f, 1.0f); }

    juce::String formatSeconds (float s)
    {
        if (s < 1.0f)
            return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
        return juce::String (s, s < 10.0f ? 1 : 0) + " s";
    }
}

const MachineInfo& machineInfo (int index)
{
    return kMachines[static_cast<std::size_t> (std::clamp (index, 0, numMachines() - 1))];
}

int numMachines() noexcept { return static_cast<int> (kMachines.size()); }

juce::String decayLabel (int)    { return "Decay"; }
juce::String preDelayLabel (int) { return "Pre-delay"; }

juce::String formatDecay (int, float seconds) { return formatSeconds (seconds); }

juce::String formatPreDelay (int, float ms) { return juce::String (juce::roundToInt (ms)) + " ms"; }

juce::String formatPercent (float percent) { return juce::String (juce::roundToInt (percent)) + "%"; }

juce::String formatTone (float tone)
{
    const int t = juce::roundToInt (tone);
    return t > 0 ? "+" + juce::String (t) : juce::String (t);
}

juce::String formatFrequency (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, hz < 10000.0f ? 1 : 0) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String formatParam1 (int machine, float percent)
{
    const float p = unit (percent);
    switch (machine)
    {
        case blossom: return juce::String (juce::roundToInt (40.0f + 260.0f * p)) + " ms";
        case aurora: return juce::String (juce::roundToInt (30.0f * std::pow (10.0f, p))) + " ms";
        case halo:
        {
            static const char* intervals[] = { "-12 st", "+7 st", "+12 st", "+12/+7 st", "+12/+19 st" };
            return intervals[std::min (static_cast<int> (p * 5.0f), 4)];
        }
        case clearNight: return juce::String (0.6f + p, 2) + juce::String::fromUTF8 (" \xc3\x97");
        default: return formatPercent (percent);
    }
}

juce::String formatParam2 (int, float percent) { return formatPercent (percent); }

} // namespace overcast::ui
