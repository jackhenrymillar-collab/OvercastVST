cmake_minimum_required(VERSION 3.22)

# Universal binary and minimum macOS version must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(Overcast VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OVERCAST_BUILD_PLUGIN "Build the JUCE plugin (VST3, AU, Standalone)" ON)
option(OVERCAST_BUILD_TESTS "Build the DSP unit tests" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# DSP library: plain C++, no JUCE, so it can be tested on any platform.
# ---------------------------------------------------------------------------
add_library(overcast_dsp STATIC
    source/dsp/machines/PlateReverb.cpp
    source/dsp/ReverbEngine.cpp)
target_include_directories(overcast_dsp PUBLIC source)
# The library is linked into plugin bundles, which are shared libraries.
set_target_properties(overcast_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(overcast_dsp PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(overcast_dsp PRIVATE /W4)
else()
    target_compile_options(overcast_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(OVERCAST_BUILD_PLUGIN)
    set(OVERCAST_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout (otherwise downloaded)")
    if(OVERCAST_JUCE_DIR)
        add_subdirectory(${OVERCAST_JUCE_DIR} juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.10
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(Overcast
        PRODUCT_NAME "Overcast"
        COMPANY_NAME "Overcast Audio"
        BUNDLE_ID "com.overcastaudio.overcast"
        PLUGIN_MANUFACTURER_CODE Ovca
        PLUGIN_CODE Ovc1
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Reverb
        COPY_PLUGIN_AFTER_BUILD ${APPLE})

    target_sources(Overcast PRIVATE
        source/PluginProcessor.cpp
        source/Parameters.cpp)

    target_compile_definitions(Overcast PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Overcast
        PRIVATE
            overcast_dsp
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(OVERCAST_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(overcast_tests
        tests/DspTests.cpp
        tests/PlateTests.cpp
        tests/EngineTests.cpp
        tests/MachineTests.cpp)
    target_link_libraries(overcast_tests PRIVATE overcast_dsp Catch2::Catch2WithMain)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(overcast_tests)
endif()
