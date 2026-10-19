# Overcast

A multi-machine stereo reverb plugin for macOS (VST3 and AU), built with JUCE 8.

**Status: Phases 1 to 3.** All 12 machines are implemented, level-matched and tested.
Switching machines crossfades, and with Spillover on the old tail rings out naturally.
The plugin uses JUCE's generic editor until the night-sky UI lands in Phase 4.

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
    Shared.h          FDN, diffusers, tap delays, antialiased saturation, pitch shifter
    Machines.h        machine list, factory and loudness trims
    machines/         one file per machine
    ReverbEngine.*    machine switching, pre-delay, tone, wet filters, limiter and mix
tests/                Catch2 tests for the DSP
```

## What the tests check

- Dry signal passes through bit-exact at 0% mix, at any buffer size
- Decay time matches the Decay knob (plate, hall and ambient machines)
- Every machine sounds the same at 44.1 and 96 kHz, and is silent for silent input
- Every machine survives 10 seconds of random parameter automation with no blow-ups
- Freeze never makes a tail grow, and holds it on every sustaining machine
- Machines are loudness matched to within 2 dB
- Switching machines, with or without spillover, never clips or produces NaN
- Wet output never exceeds full scale, even with input at +12 dB and Freeze on

CI also runs `auval` and `pluginval` (strictness 10) on every pull request.

## Parameters

| Parameter | Range |
| --- | --- |
| Machine | 12 choices (see below) |
| Decay | 0.1 to 60 s |
| Pre-delay | 0 to 500 ms |
| Mix | 0 to 100% |
| Tone | -100 to +100 (tilt EQ on the wet signal) |
| Mod | 0 to 100% |
| Param 1 / Param 2 | 0 to 100%, meaning depends on the machine (see below) |
| Low Cut / High Cut | 20 Hz to 1 kHz / 1 to 20 kHz on the wet signal |
| Freeze, Spillover, Kill Dry | on/off |
| Mix Law | Equal power or Pedal (dry stays full up to 50%) |

## Machines

| Machine | Character | Param 1 | Param 2 |
| --- | --- | --- | --- |
| Chamber | Small to medium rooms | Room size | Early / late balance |
| Arena | Large hall | Low decay (0.5x to 2x) | High decay (0.15x to 1x) |
| Sheet | Studio plate | Low end cut | Damping |
| Coil | Spring tank | Springs (1 to 3) | Dwell (drive) |
| Rise | Auto-swell | Rise time (50 ms to 3 s) | Mode: reverb only / reverb and dry |
| Blossom | Tail that grows after the attack | Bloom length | Bloom feedback |
| Haze | Huge ambient wash | Diffusion | Smear |
| Choir | Hall with vocal formants | Vowel (A, E, I, O, U) | Formant mix |
| Halo | Shimmer | Interval (-12, +5, +7, +12, +19) | Shimmer amount |
| Tape Echo | Three-head tape echo into a hall | Head pattern (7 combinations) | Feedback |
| Gate | Gated, reverse and swell envelopes | Shape (gate to reverse to swell) | Diffusion |
| Space Map | Modelled room reflections | Room shape (small room to long hall) | Listener distance |

Notes:
- **Tape Echo** uses the Pre-delay knob as its echo time (40 to 800 ms) instead of a pre-delay.
- **Gate** uses the Decay knob as its gate time (80 ms to 1.5 s).
- **Coil** limits Decay to 0.3 to 12 s, like a real spring tank.
