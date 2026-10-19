#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace overcast
{

// Each machine keeps its own knob settings. Turning the Machine selector stores the
// knobs of the machine you leave and hands back the ones the next machine had, so
// coming back to a machine finds it the way you left it.
//
// Plain C++ with no locking: the processor guards it and does the JUCE side.
class MachineMemory
{
public:
    static constexpr std::size_t kNumKnobs = 7; // decay, pre-delay, mix, tone, mod, param 1, param 2
    using Knobs = std::array<float, kNumKnobs>;

    explicit MachineMemory (std::size_t numMachines) : count (numMachines) {}

    std::size_t numMachines() const noexcept { return count; }
    int current() const noexcept { return active; }

    // Start tracking a machine without storing or restoring anything (for example
    // after a preset or a session sets every knob at once).
    void setCurrent (int machine) noexcept { active = machine; }

    void store (int machine, const Knobs& knobs) noexcept
    {
        if (valid (machine))
            slots[static_cast<std::size_t> (machine)] = knobs;
    }

    std::optional<Knobs> recall (int machine) const noexcept
    {
        return valid (machine) ? slots[static_cast<std::size_t> (machine)] : std::nullopt;
    }

    void clear (int machine) noexcept
    {
        if (valid (machine))
            slots[static_cast<std::size_t> (machine)].reset();
    }

    // The selector moved to `machine` while the knobs read `knobs`. Stores them for
    // the machine being left and returns the knobs to apply, if `machine` has any.
    std::optional<Knobs> switchTo (int machine, const Knobs& knobs) noexcept
    {
        if (machine == active)
            return std::nullopt;
        store (active, knobs);
        active = machine;
        return recall (machine);
    }

private:
    bool valid (int machine) const noexcept
    {
        return machine >= 0 && static_cast<std::size_t> (machine) < count && static_cast<std::size_t> (machine) < kMaxMachines;
    }

    static constexpr std::size_t kMaxMachines = 32;
    std::size_t count;
    int active = -1;
    std::array<std::optional<Knobs>, kMaxMachines> slots {};
};

} // namespace overcast
