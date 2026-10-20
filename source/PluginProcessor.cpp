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
    bypass   = state.getRawParameterValue (ParamID::bypass);
    bypassParam = state.getParameter (ParamID::bypass);
    knobs = { decay, preDelay, mix, tone, mod, param1, param2 };

    // Machines start from their factory preset's knobs until you change them.
    for (int m = 0; m < static_cast<int> (memory.numMachines()); ++m)
        if (const auto k = presets.getFactoryKnobs (m))
            memory.store (m, *k);
    memory.setCurrent (currentMachine());
    memory.store (currentMachine(), readKnobs());

    presets.onBeforeApply = [this] { beginBulkChange(); };
    presets.onAfterApply = [this] { endBulkChange(); };
    state.addParameterListener (ParamID::machine, this);
}

OvercastProcessor::~OvercastProcessor()
{
    state.removeParameterListener (ParamID::machine, this);
    cancelPendingUpdate();
}

namespace
{
    // Parameter IDs of the knobs each machine remembers, in MachineMemory order.
    constexpr std::array<const char*, MachineMemory::kNumKnobs> kKnobIds {
        ParamID::decay, ParamID::preDelay, ParamID::mix, ParamID::tone,
        ParamID::mod, ParamID::param1, ParamID::param2 };
    constexpr auto kMemoryTag = "MACHINE_KNOBS";
}

int OvercastProcessor::currentMachine() const noexcept
{
    return juce::roundToInt (machine->load());
}

MachineMemory::Knobs OvercastProcessor::readKnobs() const noexcept
{
    MachineMemory::Knobs k {};
    for (std::size_t i = 0; i < k.size(); ++i)
        k[i] = knobs[i]->load();
    return k;
}

void OvercastProcessor::applyKnobs (const MachineMemory::Knobs& k)
{
    for (std::size_t i = 0; i < k.size(); ++i)
        if (auto* param = state.getParameter (kKnobIds[i]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (k[i]));
            param->endChangeGesture();
        }
}

void OvercastProcessor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (host automation), so the work is deferred.
    if (! holdMemory.load())
        triggerAsyncUpdate();
}

void OvercastProcessor::handleAsyncUpdate()
{
    const juce::ScopedLock sl (memoryLock);
    if (holdMemory.load())
        return;
    if (const auto restored = memory.switchTo (currentMachine(), readKnobs()))
    {
        holdMemory = true;
        applyKnobs (*restored);
        holdMemory = false;
    }
}

void OvercastProcessor::beginBulkChange()
{
    // Finish any machine change still waiting, then keep the knobs of the machine
    // being left before the preset overwrites them.
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleUpdateNowIfNeeded();
    const juce::ScopedLock sl (memoryLock);
    holdMemory = true;
    memory.store (memory.current(), readKnobs());
}

void OvercastProcessor::endBulkChange()
{
    // The preset's knobs become the memory of the machine it selected.
    cancelPendingUpdate();
    const juce::ScopedLock sl (memoryLock);
    memory.setCurrent (currentMachine());
    memory.store (currentMachine(), readKnobs());
    holdMemory = false;
}

void OvercastProcessor::writeMemory (juce::ValueTree& tree) const
{
    juce::ValueTree node (kMemoryTag);
    const juce::ScopedLock sl (memoryLock);
    for (int m = 0; m < static_cast<int> (memory.numMachines()); ++m)
        if (const auto k = memory.recall (m))
        {
            juce::ValueTree slot ("MACHINE");
            slot.setProperty ("index", m, nullptr);
            for (std::size_t i = 0; i < k->size(); ++i)
                slot.setProperty (kKnobIds[i], (*k)[i], nullptr);
            node.appendChild (slot, nullptr);
        }
    tree.appendChild (node, nullptr);
}

void OvercastProcessor::readMemory (const juce::ValueTree& node)
{
    for (const auto& slot : node)
    {
        const int m = slot.getProperty ("index", -1);
        if (m < 0 || m >= static_cast<int> (memory.numMachines()))
            continue;
        // Anything missing keeps what the machine already had.
        auto k = memory.recall (m).value_or (readKnobs());
        for (std::size_t i = 0; i < k.size(); ++i)
            k[i] = static_cast<float> (slot.getProperty (kKnobIds[i], k[i]));
        memory.store (m, k);
    }
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
    dryCopy.setSize (2, juce::jmax (1, samplesPerBlock));
    bypassStep = static_cast<float> (1.0 / (0.02 * sampleRate));
    bypassMix = bypass->load() > 0.5f ? 0.0f : 1.0f;
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

    const float target = bypass->load() > 0.5f ? 0.0f : 1.0f;
    if (target < 0.5f && bypassMix <= 0.0f)
    {
        // Fully bypassed: the dry signal passes untouched. The reverb hears silence, so
        // its tail dies away out of earshot and nothing old comes back on un-bypass.
        dryCopy.setSize (2, numSamples, false, false, true);
        dryCopy.clear();
        engine.setParams (readParams());
        engine.process (dryCopy.getWritePointer (0), dryCopy.getWritePointer (1), numSamples);
        return;
    }

    if (juce::exactlyEqual (target, bypassMix))
    {
        engine.setParams (readParams());
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        return;
    }

    // Crossfading in or out of bypass.
    dryCopy.setSize (2, numSamples, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        dryCopy.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, numSamples);
    engine.setParams (readParams());
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
    float g = bypassMix;
    for (int i = 0; i < numSamples; ++i)
    {
        g = target > g ? juce::jmin (target, g + bypassStep) : juce::jmax (target, g - bypassStep);
        for (int ch = 0; ch < 2; ++ch)
            out[ch][i] = dryCopy.getSample (ch, i) + g * (out[ch][i] - dryCopy.getSample (ch, i));
    }
    bypassMix = g;
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
    copy.setProperty ("machineLayout", kMachineLayout, nullptr);
    writeMemory (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void OvercastProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto memoryNode = tree.getChildWithName (kMemoryTag);
            tree.removeChild (memoryNode, nullptr);

            // Sessions from before the machine list was cut to six: point the selector and
            // each machine's remembered knobs at the new order, and drop removed machines.
            const bool oldLayout = static_cast<int> (tree.getProperty ("machineLayout", 1)) < kMachineLayout;
            int oldMachine = -1;
            if (oldLayout)
            {
                for (auto child : tree)
                    if (child.hasType ("PARAM") && child.getProperty ("id").toString() == ParamID::machine)
                    {
                        oldMachine = juce::roundToInt (static_cast<float> (child.getProperty ("value")));
                        child.setProperty ("value", dsp::migrateMachineIndex (oldMachine), nullptr);
                    }
                for (int i = memoryNode.getNumChildren(); --i >= 0;)
                {
                    auto slot = memoryNode.getChild (i);
                    const int m = slot.getProperty ("index", -1);
                    if (dsp::oldMachineKept (m))
                        slot.setProperty ("index", dsp::migrateMachineIndex (m), nullptr);
                    else
                        memoryNode.removeChild (i, nullptr);
                }
            }

            holdMemory = true;
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

            // Sessions saved before knob memory existed keep the factory starting points.
            cancelPendingUpdate();
            const juce::ScopedLock sl (memoryLock);
            readMemory (memoryNode);
            // A removed machine's knobs mean something else on its replacement, so the
            // replacement starts from its own remembered (or factory) knobs.
            if (oldLayout && oldMachine >= 0 && ! dsp::oldMachineKept (oldMachine))
                if (const auto k = memory.recall (currentMachine()))
                    applyKnobs (*k);
            memory.setCurrent (currentMachine());
            memory.store (currentMachine(), readKnobs());
            holdMemory = false;
        }
}

} // namespace overcast

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new overcast::OvercastProcessor();
}
