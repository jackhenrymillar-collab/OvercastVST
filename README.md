# Overcast

A multi-machine stereo reverb plugin for macOS (VST3 and AU), built with JUCE 8.

**Status: Phase 1.** The project builds, all parameters exist, and the first machine,
**Sheet** (a studio plate), is implemented. The other 11 machines are listed in the
Machine parameter but currently fall back to Sheet. The plugin uses JUCE's generic
editor until the night-sky UI lands in Phase 4.

## Building on macOS

Requirements: Xcode 15 or later (command line tools are enough) and CMake 3.22+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

JUCE and Catch2 are downloaded automatically on the first configure. To use a local
JUCE checkout instead, add `-DOVERCAST_JUCE_DIR=/path/to/JUCE`.

The build produces a universal (Apple Silicon + Intel) VST3, AU and Standalone app in
`build/Overcast_artefacts/Release/`, and copies the VST3 and AU into
`~/Library/Audio/Plug-Ins/` so your DAW can find them. In Logic, rescan with
Settings › Plug-in Manager if Overcast doesn't appear.

To work in Xcode: `cmake -S . -B build-xcode -G Xcode`.

## Layout

```
source/
  PluginProcessor.*   audio callback, parameter tree, state save/load
  Parameters.*        parameter IDs and ranges (IDs must never change)
  dsp/                plain C++ DSP with no JUCE dependency
    Blocks.h          delay lines, allpasses, filters, smoothers, LFO, limiter
    ReverbMachine.h   the interface every machine implements
    PlateReverb.*     Sheet: Dattorro-style plate
    ReverbEngine.*    pre-delay, tone, wet filters, limiter and mix around a machine
tests/                Catch2 tests for the DSP
```

## What the tests check

- Dry signal passes through bit-exact at 0% mix, at any buffer size
- Plate decay time matches the Decay knob (within 20%) and is the same at 44.1 to 192 kHz
- Left and right outputs are decorrelated
- Freeze holds the tail without decaying or growing
- No blow-ups under 20 seconds of random parameter automation
- Wet output never exceeds full scale, even with input at +12 dB and Freeze on

CI also runs `auval` and `pluginval` (strictness 10) on every pull request.

## Parameters

| Parameter | Range |
| --- | --- |
| Machine | 12 choices (Sheet implemented) |
| Decay | 0.1 to 60 s |
| Pre-delay | 0 to 500 ms |
| Mix | 0 to 100% |
| Tone | -100 to +100 (tilt EQ on the wet signal) |
| Mod | 0 to 100% |
| Param 1 / Param 2 | 0 to 100% (Sheet: low end cut / damping) |
| Low Cut / High Cut | 20 Hz to 1 kHz / 1 to 20 kHz on the wet signal |
| Freeze, Spillover, Kill Dry | on/off |
| Mix Law | Equal power or Pedal (dry stays full up to 50%) |
