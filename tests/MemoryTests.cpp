#include "MachineMemory.h"

#include <catch2/catch_test_macros.hpp>

using overcast::MachineMemory;

TEST_CASE ("Each machine keeps its own knob settings")
{
    MachineMemory memory (12);
    memory.setCurrent (1);

    const MachineMemory::Knobs arena { 5.0f, 30.0f, 35.0f, -10.0f, 45.0f, 60.0f, 40.0f };
    const MachineMemory::Knobs nebula { 10.0f, 20.0f, 45.0f, -10.0f, 40.0f, 50.0f, 60.0f };
    memory.store (9, nebula);

    // Leaving Arena stores its knobs and hands back Nebula's.
    auto restored = memory.switchTo (9, arena);
    REQUIRE (restored.has_value());
    CHECK (*restored == nebula);
    CHECK (memory.current() == 9);

    // Edit a knob on Nebula, then go back to Arena: Arena is as it was.
    auto edited = nebula;
    edited[5] = 90.0f;
    restored = memory.switchTo (1, edited);
    REQUIRE (restored.has_value());
    CHECK (*restored == arena);

    // And Nebula remembers the edit.
    restored = memory.switchTo (9, arena);
    REQUIRE (restored.has_value());
    CHECK ((*restored)[5] == 90.0f);
}

TEST_CASE ("A machine with nothing stored keeps the current knobs")
{
    MachineMemory memory (12);
    memory.setCurrent (0);
    const MachineMemory::Knobs knobs { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    CHECK_FALSE (memory.switchTo (4, knobs).has_value());
    CHECK (memory.recall (0) == knobs);

    // Re-selecting the same machine changes nothing.
    CHECK_FALSE (memory.switchTo (4, knobs).has_value());
    CHECK_FALSE (memory.recall (4).has_value());

    // Out-of-range machines are ignored.
    memory.store (40, knobs);
    CHECK_FALSE (memory.recall (40).has_value());
}
