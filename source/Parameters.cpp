#include "Parameters.h"

namespace overcast
{

juce::StringArray machineNames()
{
    return { "Blossom", "Haze", "Aurora", "Halo", "Nebula", "Clear Night" };
}

namespace
{
    using Range = juce::NormalisableRange<float>;

    Range skewed (float min, float max, float centre)
    {
        Range r { min, max };
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes withLabel (const juce::String& label, int decimals = 1)
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel (label)
            .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // parameter version hint for hosts

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::machine, v }, "Machine", machineNames(), 2)); // Aurora
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::decay, v }, "Decay", skewed (0.1f, 60.0f, 4.0f), 3.0f, withLabel ("s", 2)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::preDelay, v }, "Pre-delay", skewed (0.0f, 500.0f, 60.0f), 20.0f, withLabel ("ms", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mix, v }, "Mix", Range { 0.0f, 100.0f, 0.1f }, 35.0f, withLabel ("%", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::tone, v }, "Tone", Range { -100.0f, 100.0f, 0.1f }, 0.0f, withLabel ("", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mod, v }, "Mod", Range { 0.0f, 100.0f, 0.1f }, 50.0f, withLabel ("%", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::param1, v }, "Param 1", Range { 0.0f, 100.0f, 0.1f }, 20.0f, withLabel ("%", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::param2, v }, "Param 2", Range { 0.0f, 100.0f, 0.1f }, 40.0f, withLabel ("%", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::lowCut, v }, "Low Cut", skewed (20.0f, 1000.0f, 150.0f), 20.0f, withLabel ("Hz", 0)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::highCut, v }, "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, withLabel ("Hz", 0)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::freeze, v }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::spillover, v }, "Spillover", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::killDry, v }, "Kill Dry", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::mixLaw, v }, "Mix Law", StringArray { "Equal power", "Pedal" }, 1));
    // Also the host's bypass switch (see OvercastProcessor::getBypassParameter). Clicking the moon toggles it.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, v }, "Bypass", false));

    return layout;
}

} // namespace overcast
