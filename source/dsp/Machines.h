#pragma once

// Every machine, in the order of the Machine parameter.
#include "machines/Arena.h"
#include "machines/Aurora.h"
#include "machines/Blossom.h"
#include "machines/Chamber.h"
#include "machines/ClearNight.h"
#include "machines/Coil.h"
#include "machines/Halo.h"
#include "machines/Haze.h"
#include "machines/Nebula.h"
#include "machines/PlateReverb.h"
#include "machines/Rise.h"
#include "machines/SpaceMap.h"

#include <memory>

namespace overcast::dsp
{

enum class MachineId
{
    chamber, arena, sheet, coil, rise, blossom, haze, aurora, halo, nebula, clearNight, spaceMap, count
};

inline constexpr int kNumMachines = static_cast<int> (MachineId::count);

inline std::unique_ptr<ReverbMachine> createMachine (MachineId id)
{
    switch (id)
    {
        case MachineId::chamber:  return std::make_unique<Chamber>();
        case MachineId::arena:    return std::make_unique<Arena>();
        case MachineId::sheet:    return std::make_unique<PlateReverb>();
        case MachineId::coil:     return std::make_unique<Coil>();
        case MachineId::rise:     return std::make_unique<Rise>();
        case MachineId::blossom:  return std::make_unique<Blossom>();
        case MachineId::haze:     return std::make_unique<Haze>();
        case MachineId::aurora:   return std::make_unique<Aurora>();
        case MachineId::halo:     return std::make_unique<Halo>();
        case MachineId::nebula:   return std::make_unique<Nebula>();
        case MachineId::clearNight: return std::make_unique<ClearNight>();
        case MachineId::spaceMap: return std::make_unique<SpaceMap>();
        case MachineId::count:    break;
    }
    return std::make_unique<PlateReverb>();
}

// Output trim per machine so switching machines keeps a similar loudness at default
// settings. Measured by the "machines are loudness matched" test (tests/MachineTests.cpp).
inline constexpr float kMachineTrim[kNumMachines] = {
    0.659f, // Chamber
    0.779f, // Arena
    1.000f, // Sheet (reference)
    0.756f, // Coil
    0.771f, // Rise
    0.686f, // Blossom
    0.839f, // Haze
    0.951f, // Aurora
    0.893f, // Halo
    1.979f, // Nebula
    0.990f, // Clear Night
    0.776f  // Space Map
};

} // namespace overcast::dsp
