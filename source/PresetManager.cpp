#include "PresetManager.h"
#include "Parameters.h"

namespace overcast
{

namespace
{
    constexpr auto kFileExtension = ".ovpreset";
    constexpr auto kRootTag = "OvercastPreset";

    struct FactoryPreset
    {
        const char* name;
        int machine;
        float decay, preDelay, mix, tone, mod, param1, param2, lowCut, highCut;
    };

    // One starting point per machine, in machine order.
    constexpr FactoryPreset kFactory[] = {
        //  name               machine decay  pre   mix   tone   mod   p1     p2    lowcut highcut
        { "Moonlit Room",      0,  1.4f,   8.0f, 30.0f,   0.0f, 30.0f,  45.0f, 55.0f,  60.0f, 12000.0f },
        { "Night Arena",       1,  5.5f,  30.0f, 35.0f, -10.0f, 45.0f,  60.0f, 40.0f,  80.0f,  9000.0f },
        { "Silver Sheet",      2,  2.6f,  20.0f, 30.0f,  15.0f, 35.0f,  40.0f, 30.0f, 100.0f, 16000.0f },
        { "Rain Coil",         3,  2.2f,   0.0f, 35.0f,  10.0f, 25.0f, 100.0f, 45.0f, 120.0f,  7000.0f },
        { "Dawn Rise",         4,  4.5f,  10.0f, 50.0f,  -5.0f, 50.0f,  55.0f, 20.0f,  80.0f, 10000.0f },
        { "Night Blossom",     5,  6.0f,  25.0f, 40.0f,   0.0f, 50.0f,  60.0f, 55.0f,  80.0f, 11000.0f },
        { "Midnight Haze",     6, 14.0f,  40.0f, 45.0f, -20.0f, 70.0f,  80.0f, 55.0f, 120.0f,  8000.0f },
        { "Aurora Glass",      7,  8.0f,  30.0f, 40.0f,  -5.0f, 45.0f,  50.0f, 30.0f,  80.0f, 11000.0f },
        { "Starlight Halo",    8,  9.0f,  30.0f, 40.0f,   5.0f, 45.0f,  70.0f, 50.0f, 150.0f, 12000.0f },
        { "Night Nebula",      9, 10.0f,  20.0f, 45.0f, -10.0f, 40.0f,  50.0f, 60.0f, 100.0f,  9000.0f },
        { "Clear Night Hall", 10,  3.5f,  15.0f, 35.0f,   0.0f, 30.0f,  55.0f, 55.0f,  60.0f, 14000.0f },
        { "Observatory",      11,  1.8f,   0.0f, 30.0f,   0.0f, 20.0f,  60.0f, 40.0f,  40.0f, 15000.0f },
    };
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    for (const auto& f : kFactory)
    {
        Preset p;
        p.name = f.name;
        p.values = { { ParamID::machine,  static_cast<float> (f.machine) },
                     { ParamID::decay,    f.decay },
                     { ParamID::preDelay, f.preDelay },
                     { ParamID::mix,      f.mix },
                     { ParamID::tone,     f.tone },
                     { ParamID::mod,      f.mod },
                     { ParamID::param1,   f.param1 },
                     { ParamID::param2,   f.param2 },
                     { ParamID::lowCut,   f.lowCut },
                     { ParamID::highCut,  f.highCut } };
        factory.push_back (std::move (p));
    }

    refreshUserPresets();
    setLoaded (captureValues());
}

void PresetManager::setLoaded (Values values)
{
    const juce::ScopedLock sl (listLock);
    loaded = std::move (values);
}

int PresetManager::getNumPresets() const
{
    const juce::ScopedLock sl (listLock);
    return static_cast<int> (factory.size() + user.size());
}

int PresetManager::getNumFactoryPresets() const noexcept { return static_cast<int> (factory.size()); }

juce::String PresetManager::getPresetName (int index) const
{
    const juce::ScopedLock sl (listLock);
    if (index < 0)
        return "Init";
    if (index < static_cast<int> (factory.size()))
        return factory[static_cast<std::size_t> (index)].name;
    index -= static_cast<int> (factory.size());
    if (index < static_cast<int> (user.size()))
        return user[static_cast<std::size_t> (index)].name;
    return {};
}

juce::String PresetManager::getCurrentName() const { return getPresetName (current.load()); }

PresetManager::Values PresetManager::captureValues() const
{
    Values v;
    for (auto* param : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            v[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
    return v;
}

std::optional<MachineMemory::Knobs> PresetManager::getFactoryKnobs (int machine) const
{
    for (const auto& f : kFactory)
        if (f.machine == machine)
            return MachineMemory::Knobs { f.decay, f.preDelay, f.mix, f.tone, f.mod, f.param1, f.param2 };
    return std::nullopt;
}

void PresetManager::applyValues (const Values& values)
{
    if (onBeforeApply)
        onBeforeApply();
    for (auto* param : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;

        // Anything the preset leaves out goes back to its default.
        const auto it = values.find (ranged->getParameterID());
        const float normalised = it != values.end() ? ranged->convertTo0to1 (it->second)
                                                    : ranged->getDefaultValue();
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (normalised);
        ranged->endChangeGesture();
    }

    if (onAfterApply)
        onAfterApply();
}

bool PresetManager::isModified() const
{
    Values loadedCopy;
    {
        const juce::ScopedLock sl (listLock);
        loadedCopy = loaded;
    }
    for (const auto& [id, value] : captureValues())
    {
        const auto it = loadedCopy.find (id);
        if (it == loadedCopy.end() || std::abs (it->second - value) > 1.0e-3f * std::max (1.0f, std::abs (value)))
            return true;
    }
    return false;
}

void PresetManager::loadPreset (int index)
{
    Values values;
    {
        const juce::ScopedLock sl (listLock);
        const int total = static_cast<int> (factory.size() + user.size());
        if (index < 0 || index >= total)
            return;
        values = index < static_cast<int> (factory.size())
                     ? factory[static_cast<std::size_t> (index)].values
                     : user[static_cast<std::size_t> (index) - factory.size()].values;
    }

    applyValues (values);
    setLoaded (captureValues());
    current = index;
    ++changeCount;
}

void PresetManager::loadNext()
{
    const int n = getNumPresets();
    if (n > 0)
        loadPreset ((current.load() + 1 + n) % n);
}

void PresetManager::loadPrevious()
{
    const int n = getNumPresets();
    if (n > 0)
        loadPreset (current.load() <= 0 ? n - 1 : current.load() - 1);
}

juce::File PresetManager::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Audio/Presets")
#endif
        .getChildFile ("Overcast Audio/Overcast");
}

bool PresetManager::readPresetFile (const juce::File& file, Preset& out)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (kRootTag))
        return false;

    out.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    out.file = file;
    out.values.clear();
    for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
        out.values[p->getStringAttribute ("id")] = static_cast<float> (p->getDoubleAttribute ("value"));
    return true;
}

void PresetManager::refreshUserPresets()
{
    std::vector<Preset> found;
    for (const auto& file : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kFileExtension))
    {
        Preset p;
        if (readPresetFile (file, p))
            found.push_back (std::move (p));
    }
    std::sort (found.begin(), found.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });

    const auto currentName = getCurrentName();
    const bool wasUser = current.load() >= static_cast<int> (factory.size());
    {
        const juce::ScopedLock sl (listLock);
        user = std::move (found);
    }

    // Keep pointing at the same user preset after the list is re-sorted.
    if (wasUser)
    {
        int index = -1;
        for (int i = 0; i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].name == currentName)
                index = static_cast<int> (factory.size()) + i;
        current = index;
    }
    ++changeCount;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    juce::XmlElement xml (kRootTag);
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (const auto& [id, value] : captureValues())
    {
        auto* p = xml.createNewChildElement ("PARAM");
        p->setAttribute ("id", id);
        p->setAttribute ("value", value);
    }

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + kFileExtension);
    if (! xml.writeTo (file))
        return false;

    refreshUserPresets();
    {
        const juce::ScopedLock sl (listLock);
        for (int i = 0; i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].file == file)
                current = static_cast<int> (factory.size()) + i;
    }
    setLoaded (captureValues());
    ++changeCount;
    return true;
}

void PresetManager::toggleAB()
{
    auto& from = onB ? slotB : slotA;
    auto& to = onB ? slotA : slotB;
    from = captureValues();
    if (to.empty())
        to = from;
    else
        applyValues (to);
    onB = ! onB;
    ++changeCount;
}

void PresetManager::writeToState (juce::ValueTree& stateCopy) const
{
    stateCopy.setProperty ("preset", current.load() >= 0 ? getCurrentName() : juce::String(), nullptr);
}

void PresetManager::readFromState (const juce::ValueTree& restored)
{
    // May be called off the message thread, so it only works out which preset is
    // current. The comparison values are filled in from the preset list.
    const auto name = restored.getProperty ("preset").toString();
    int index = -1;
    Values values;
    if (name.isNotEmpty())
    {
        const juce::ScopedLock sl (listLock);
        for (int i = 0; index < 0 && i < static_cast<int> (factory.size()); ++i)
            if (factory[static_cast<std::size_t> (i)].name == name)
                index = i;
        for (int i = 0; index < 0 && i < static_cast<int> (user.size()); ++i)
            if (user[static_cast<std::size_t> (i)].name == name)
                index = static_cast<int> (factory.size()) + i;
    }

    // Fill in defaults for anything a preset leaves out, the same way applyValues does.
    if (index >= 0)
    {
        const juce::ScopedLock sl (listLock);
        const auto& preset = index < static_cast<int> (factory.size())
                                 ? factory[static_cast<std::size_t> (index)]
                                 : user[static_cast<std::size_t> (index) - factory.size()];
        for (auto* param : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const auto it = preset.values.find (ranged->getParameterID());
                values[ranged->getParameterID()] = it != preset.values.end()
                    ? ranged->convertFrom0to1 (ranged->convertTo0to1 (it->second))
                    : ranged->convertFrom0to1 (ranged->getDefaultValue());
            }
    }
    else
    {
        values = captureValues();
    }

    setLoaded (std::move (values));
    current = index;
    ++changeCount;
}

} // namespace overcast
