#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MachineMemory.h"

#include <atomic>
#include <functional>
#include <map>

namespace overcast
{

// Factory and user presets, plus the A/B compare slots.
//
// Presets store real parameter values by parameter ID, so they keep working if
// a range changes. User presets are XML files in
// ~/Library/Audio/Presets/Overcast Audio/Overcast (the usual macOS location).
//
// Loading, saving and A/B run on the message thread.
class PresetManager final
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    int getNumPresets() const;
    int getNumFactoryPresets() const noexcept;
    juce::String getPresetName (int index) const;

    // -1 when no preset is loaded (the plugin's defaults).
    int getCurrentIndex() const noexcept { return current.load(); }
    juce::String getCurrentName() const;
    // True when the knobs no longer match the loaded preset. Message thread only.
    bool isModified() const;

    void loadPreset (int index);
    void loadNext();
    void loadPrevious();

    // Returns false if the file couldn't be written.
    bool saveUserPreset (const juce::String& name);
    void refreshUserPresets();
    static juce::File getUserPresetFolder();

    // A/B compare. The first switch copies A into B, so you can tweak B and compare.
    bool isOnB() const noexcept { return onB; }
    void toggleAB();

    // Session save and restore: remembers which preset was loaded.
    void writeToState (juce::ValueTree& stateCopy) const;
    void readFromState (const juce::ValueTree& restored);

    // The knob settings of the factory preset for a machine, in MachineMemory order.
    std::optional<MachineMemory::Knobs> getFactoryKnobs (int machine) const;

    // Called around every preset load and A/B switch, which set all the knobs at once.
    std::function<void()> onBeforeApply, onAfterApply;

    // Bumped whenever the preset list, the current preset or the A/B slot changes.
    int getChangeCount() const noexcept { return changeCount.load(); }

private:
    using Values = std::map<juce::String, float>;

    struct Preset
    {
        juce::String name;
        Values values;
        juce::File file; // empty for factory presets
    };

    Values captureValues() const;
    void applyValues (const Values&);
    void setLoaded (Values);
    static bool readPresetFile (const juce::File&, Preset&);

    juce::AudioProcessorValueTreeState& state;
    std::vector<Preset> factory;
    std::vector<Preset> user;
    mutable juce::CriticalSection listLock;

    std::atomic<int> current { -1 };
    std::atomic<int> changeCount { 0 };

    bool onB = false;
    Values slotA, slotB;
    Values loaded; // what the current preset set, to tell when it's been changed
};

} // namespace overcast
