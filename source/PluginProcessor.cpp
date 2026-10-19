#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace overcast
{

OvercastProcessor::OvercastProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Overcast", createParameterLayout())
{
    machine  = state.getRawParameterValue (ParamID::machine);
    decay    = state.getRawParameterValue (ParamID::decay);
    preDelay = state.getRawParameterValue (ParamID::preDelay);
    mix      = state.getRawParameterValue (ParamID::mix);
    tone     = state.getRawParameterValue (ParamID::tone);
    mod      = state.getRawParameterValue (ParamID::mod);
    param1   = state.getRawParameterValue (ParamID::param1);
    param2   = state.getRawParameterValue (ParamID::param2);
    lowCut   = state.getRawParameterValue (ParamID::lowCut);
    highCut  = state.getRawParameterValue (ParamID::highCut);
    freeze   = state.getRawParameterValue (ParamID::freeze);
    killDry  = state.getRawParameterValue (ParamID::killDry);
    spillover = state.getRawParameterValue (ParamID::spillover);
    mixLaw   = state.getRawParameterValue (ParamID::mixLaw);
}

bool OvercastProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono-in/stereo-out and stereo/stereo.
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void OvercastProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
}

void OvercastProcessor::reset()
{
    engine.reset();
}

dsp::EngineParams OvercastProcessor::readParams() const noexcept
{
    dsp::EngineParams p;
    p.machine = static_cast<int> (machine->load());
    p.machineParams.decaySeconds = decay->load();
    p.machineParams.mod = mod->load() * 0.01f;
    p.machineParams.param1 = param1->load() * 0.01f;
    p.machineParams.param2 = param2->load() * 0.01f;
    p.machineParams.freeze = freeze->load() > 0.5f;
    p.preDelayMs = preDelay->load();
    p.mix = mix->load() * 0.01f;
    p.tone = tone->load() * 0.01f;
    p.lowCutHz = lowCut->load();
    p.highCutHz = highCut->load();
    p.killDry = killDry->load() > 0.5f;
    p.spillover = spillover->load() > 0.5f;
    p.mixLaw = mixLaw->load() > 0.5f ? dsp::MixLaw::pedal : dsp::MixLaw::equalPower;
    return p;
}

void OvercastProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Mono input: duplicate into the right channel so the dry signal is centred.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    engine.setParams (readParams());
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
}

juce::AudioProcessorEditor* OvercastProcessor::createEditor()
{
    return new OvercastEditor (*this);
}

int OvercastProcessor::getNumPrograms()
{
    return presets.getNumFactoryPresets();
}

int OvercastProcessor::getCurrentProgram()
{
    const int index = presets.getCurrentIndex();
    return index >= 0 && index < presets.getNumFactoryPresets() ? index : 0;
}

void OvercastProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < presets.getNumFactoryPresets())
        presets.loadPreset (index);
}

const juce::String OvercastProcessor::getProgramName (int index)
{
    return index >= 0 && index < presets.getNumFactoryPresets() ? presets.getPresetName (index) : juce::String();
}

void OvercastProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    presets.writeToState (copy);
    copy.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void OvercastProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            state.replaceState (tree);

            // replaceState skips a parameter whose stored value already matches, judged
            // after snapping. A switch left at a raw 0.3 (reads as off) would then never
            // be set back to exactly 0, so set every parameter explicitly.
            for (const auto& child : tree)
                if (child.hasType ("PARAM"))
                    if (auto* param = state.getParameter (child.getProperty ("id").toString()))
                        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (child.getProperty ("value"))));

            presets.readFromState (tree);
            uiScale = static_cast<float> (tree.getProperty ("uiScale", 1.0));
        }
}

} // namespace overcast

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new overcast::OvercastProcessor();
}
