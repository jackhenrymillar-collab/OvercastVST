#pragma once

#include "MachineMemory.h"
#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/ReverbEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace overcast
{

class OvercastProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    OvercastProcessor();
    ~OvercastProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 60.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Hosts see the factory presets as programs.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PresetManager& getPresets() noexcept { return presets; }

    // The panel's size, remembered with the session.
    float getUiScale() const noexcept { return uiScale.load(); }
    void setUiScale (float s) noexcept { uiScale = s; }

private:
    dsp::EngineParams readParams() const noexcept;

    // Per-machine knob memory (see MachineMemory.h). Machine changes are handled on
    // the message thread; preset loads and session restores set the memory directly.
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void beginBulkChange();
    void endBulkChange();
    int currentMachine() const noexcept;
    MachineMemory::Knobs readKnobs() const noexcept;
    void applyKnobs (const MachineMemory::Knobs&);
    void writeMemory (juce::ValueTree&) const;
    void readMemory (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState state;
    PresetManager presets { state };
    std::atomic<float> uiScale { 1.0f };
    dsp::ReverbEngine engine;

    MachineMemory memory { static_cast<std::size_t> (machineNames().size()) };
    mutable juce::CriticalSection memoryLock;
    std::atomic<bool> holdMemory { false };
    std::array<std::atomic<float>*, MachineMemory::kNumKnobs> knobs {};

    // Cached raw parameter pointers: lock-free reads on the audio thread.
    std::atomic<float>* machine = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* preDelay = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* tone = nullptr;
    std::atomic<float>* mod = nullptr;
    std::atomic<float>* param1 = nullptr;
    std::atomic<float>* param2 = nullptr;
    std::atomic<float>* lowCut = nullptr;
    std::atomic<float>* highCut = nullptr;
    std::atomic<float>* freeze = nullptr;
    std::atomic<float>* killDry = nullptr;
    std::atomic<float>* spillover = nullptr;
    std::atomic<float>* mixLaw = nullptr;
    std::atomic<float>* bypass = nullptr;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    // Bypass fades between the effect and the dry input over about 20 ms.
    juce::AudioBuffer<float> dryCopy;
    float bypassMix = 1.0f, bypassStep = 0.0f; // 1 = effect on, 0 = bypassed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvercastProcessor)
};

} // namespace overcast
