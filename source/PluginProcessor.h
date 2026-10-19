#pragma once

#include "Parameters.h"
#include "dsp/ReverbEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace overcast
{

class OvercastProcessor final : public juce::AudioProcessor
{
public:
    OvercastProcessor();

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

private:
    dsp::EngineParams readParams() const noexcept;

    juce::AudioProcessorValueTreeState state;
    dsp::ReverbEngine engine;

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
    std::atomic<float>* mixLaw = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvercastProcessor)
};

} // namespace overcast
