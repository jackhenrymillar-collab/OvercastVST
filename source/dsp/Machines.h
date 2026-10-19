#pragma once

// Every machine, in the order of the Machine parameter.
#include "machines/Arena.h"
#include "machines/Blossom.h"
#include "machines/Chamber.h"
#include "machines/Choir.h"
#include "machines/Coil.h"
#include "machines/Gate.h"
#include "machines/Halo.h"
#include "machines/Haze.h"
#include "machines/PlateReverb.h"
#include "machines/Rise.h"
#include "machines/SpaceMap.h"
#include "machines/TapeEcho.h"

#include <memory>

namespace overcast::dsp
{

enum class MachineId
{
    chamber, arena, sheet, coil, rise, blossom, haze, choir, halo, tapeEcho, gate, spaceMap, count
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
        case MachineId::choir:    return std::make_unique<Choir>();
        case MachineId::halo:     return std::make_unique<Halo>();
        case MachineId::tapeEcho: return std::make_unique<TapeEcho>();
        case MachineId::gate:     return std::make_unique<Gate>();
        case MachineId::spaceMap: return std::make_unique<SpaceMap>();
        case MachineId::count:    break;
    }
    return std::make_unique<PlateReverb>();
}

// Output trim per machine so switching machines keeps a similar loudness at default
// settings. Measured by the "machines are loudness matched" test (tests/MachineTests.cpp).
inline constexpr float kMachineTrim[kNumMachines] = {
    0.662f, // Chamber
    0.767f, // Arena
    1.000f, // Sheet (reference)
    0.756f, // Coil
    0.748f, // Rise
    0.667f, // Blossom
    0.834f, // Haze
    1.044f, // Choir
    0.873f, // Halo
    1.966f, // Tape Echo
    0.776f, // Gate
    0.795f  // Space Map
};

} // namespace overcast::dsp
