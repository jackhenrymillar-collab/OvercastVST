#include "MachineInfo.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace overcast::ui
{

namespace
{
    // Keep in step with the machines in source/dsp/machines and the README table.
    constexpr std::array<MachineInfo, 12> kMachines { {
        { "Chamber",   "Small to medium rooms with a short, dense tail",        "Size",       "Early / late" },
        { "Arena",     "A large concert hall that builds slowly and spreads wide", "Low decay", "High decay" },
        { "Sheet",     "A bright, dense studio plate",                          "Low cut",    "Damping" },
        { "Coil",      "A drippy amp-style spring tank",                        "Springs",    "Dwell" },
        { "Rise",      "Reverb that swells in behind every note",               "Rise time",  "Swell mode" },
        { "Blossom",   "A tail that slowly grows denser after the attack",      "Length",     "Feedback" },
        { "Haze",      "A huge, diffuse ambient wash with no clear echoes",     "Diffusion",  "Smear" },
        { "Choir",     "Reverb shaped by formant filters that sound like voices", "Vowel",    "Formant mix" },
        { "Halo",      "Octave-up shimmer from pitch-shifted feedback",         "Interval",   "Shimmer" },
        { "Tape Echo", "Multi-head tape echo feeding a reverb",                 "Heads",      "Feedback" },
        { "Gate",      "Gated, reverse and swelling envelopes",                 "Shape",      "Diffusion" },
        { "Space Map", "Early reflections that place a sound in a room",        "Room shape", "Distance" },
    } };

    enum Machine { chamber, arena, sheet, coil, rise, blossom, haze, choir, halo, tapeEcho, gate, spaceMap };

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

juce::String decayLabel (int machine)    { return machine == gate ? "Gate time" : "Decay"; }
juce::String preDelayLabel (int machine) { return machine == tapeEcho ? "Echo time" : "Pre-delay"; }

juce::String formatDecay (int machine, float seconds)
{
    if (machine == gate)
        return formatSeconds (std::clamp (seconds, 0.08f, 1.5f)); // Gate.h clamps its gate time
    return formatSeconds (seconds);
}

juce::String formatPreDelay (int machine, float ms)
{
    if (machine == tapeEcho)
        ms = std::clamp (ms, 40.0f, 800.0f); // TapeEcho.h clamps the head spacing
    return juce::String (juce::roundToInt (ms)) + " ms";
}

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
        case arena: return juce::String (std::pow (2.0f, 2.0f * p - 1.0f), 2) + juce::String::fromUTF8 (" \xc3\x97");
        case sheet: return formatFrequency (5.0f * std::pow (80.0f, p));
        case coil:  return juce::String (1 + static_cast<int> (p * 2.999f));
        case rise:  return formatSeconds (0.05f * std::pow (60.0f, p));
        case blossom: return juce::String (juce::roundToInt (40.0f + 260.0f * p)) + " ms";
        case choir:
        {
            static const char* vowels[] = { "A", "E", "I", "O", "U" };
            const float v = p * 4.0f;
            const int lo = static_cast<int> (std::floor (v));
            const float frac = v - static_cast<float> (lo);
            if (frac < 0.2f || lo >= 4) return vowels[std::min (lo, 4)];
            if (frac > 0.8f) return vowels[lo + 1];
            return juce::String (vowels[lo]) + " / " + vowels[lo + 1];
        }
        case halo:
        {
            static const char* intervals[] = { "-12 st", "+5 st", "+7 st", "+12 st", "+19 st" };
            return intervals[std::min (static_cast<int> (p * 5.0f), 4)];
        }
        case tapeEcho:
        {
            static const char* patterns[] = { "1", "2", "3", "1 + 2", "2 + 3", "1 + 3", "1 + 2 + 3" };
            return patterns[std::min (static_cast<int> (p * 7.0f), 6)];
        }
        case gate:
        {
            const float s = p * 2.0f;
            if (s < 0.25f) return "Gate";
            if (s < 0.75f) return "Gate / Rev";
            if (s < 1.25f) return "Reverse";
            if (s < 1.75f) return "Rev / Swell";
            return "Swell";
        }
        default: return formatPercent (percent);
    }
}

juce::String formatParam2 (int machine, float percent)
{
    const float p = unit (percent);
    switch (machine)
    {
        case arena: return juce::String (0.15f + 0.85f * p, 2) + juce::String::fromUTF8 (" \xc3\x97");
        case sheet: return formatFrequency (16000.0f * std::pow (1500.0f / 16000.0f, p));
        case rise:  return p >= 0.5f ? "Wet + dry" : "Wet only";
        default:    return formatPercent (percent);
    }
}

} // namespace overcast::ui
