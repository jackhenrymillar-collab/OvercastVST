#include "PluginProcessor.h"

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
    // Phase 1 uses the generic editor. The night-sky UI arrives in Phase 4.
    return new juce::GenericAudioProcessorEditor (*this);
}

void OvercastProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OvercastProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace overcast

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new overcast::OvercastProcessor();
}
