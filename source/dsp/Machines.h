#pragma once

// Every machine, in the order of the Machine parameter.
#include "machines/Aurora.h"
#include "machines/Blossom.h"
#include "machines/ClearNight.h"
#include "machines/Halo.h"
#include "machines/Haze.h"
#include "machines/Nebula.h"

#include <memory>

namespace overcast::dsp
{

// Sessions saved before 2026-10-20 used a twelve-machine list; see migrateMachineIndex.
enum class MachineId
{
    blossom, haze, aurora, halo, nebula, clearNight, count
};

inline constexpr int kNumMachines = static_cast<int> (MachineId::count);

inline std::unique_ptr<ReverbMachine> createMachine (MachineId id)
{
    switch (id)
    {
        case MachineId::blossom:    return std::make_unique<Blossom>();
        case MachineId::haze:       return std::make_unique<Haze>();
        case MachineId::aurora:     return std::make_unique<Aurora>();
        case MachineId::halo:       return std::make_unique<Halo>();
        case MachineId::nebula:     return std::make_unique<Nebula>();
        case MachineId::clearNight: return std::make_unique<ClearNight>();
        case MachineId::count:      break;
    }
    return std::make_unique<Aurora>();
}

// The old twelve-machine order was Chamber, Arena, Sheet, Coil, Rise, Blossom, Haze,
// Aurora, Halo, Nebula, Clear Night, Space Map. Machines that were removed open on the
// closest one left: the rooms, hall, plate and spring on Clear Night, Rise on Haze
// (which took over its sunrise sky) and Space Map on Nebula.
inline constexpr int migrateMachineIndex (int oldIndex) noexcept
{
    constexpr MachineId map[] = { MachineId::clearNight, MachineId::clearNight, MachineId::clearNight,
                                  MachineId::clearNight, MachineId::haze,       MachineId::blossom,
                                  MachineId::haze,       MachineId::aurora,     MachineId::halo,
                                  MachineId::nebula,     MachineId::clearNight, MachineId::nebula };
    return oldIndex >= 0 && oldIndex < 12 ? static_cast<int> (map[oldIndex]) : static_cast<int> (MachineId::aurora);
}

// True when a machine from the old list was kept (so its remembered knobs still apply).
inline constexpr bool oldMachineKept (int oldIndex) noexcept
{
    return oldIndex >= 5 && oldIndex <= 10;
}

// Output trim per machine so switching machines keeps a similar loudness at default
// settings. Measured by the "machines are loudness matched" test (tests/MachineTests.cpp),
// against the level the removed Sheet plate had with no trim.
inline constexpr float kMachineTrim[kNumMachines] = {
    0.686f, // Blossom
    0.839f, // Haze
    0.893f, // Aurora
    0.893f, // Halo
    1.979f, // Nebula
    0.990f  // Clear Night
};

} // namespace overcast::dsp
