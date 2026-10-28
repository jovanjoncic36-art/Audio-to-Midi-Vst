cmake_minimum_required(VERSION 3.22)

# Intel Macs (e.g. MacBook Pro 2017) and macOS 10.15+ by default. These must be set before project().
# Override with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Architectures to build (x86_64 = Intel)")
endif()

project(CutbenchNotes VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CUTBENCH_BUILD_PLUGIN "Build the JUCE plugin (VST3 / AU / Standalone)" ON)
option(CUTBENCH_BUILD_TESTS  "Build the unit tests for the analysis core" ON)
option(CUTBENCH_COPY_PLUGIN  "Copy the plugins into ~/Library/Audio/Plug-Ins after building (macOS)" ON)
set(CUTBENCH_JUCE_DIR "" CACHE PATH "Use this local JUCE checkout instead of downloading JUCE 8.0.4")

# ------------------------------------------------------------------------------
# Analysis core: no JUCE dependency (pitch analysis, notes, MIDI export, threading, preview synth)
# ------------------------------------------------------------------------------
find_package(Threads REQUIRED)

add_library(cutbench_core STATIC
    core/src/AnalysisEngine.cpp
    core/src/Candidates.cpp
    core/src/Fft.cpp
    core/src/MidiFile.cpp
    core/src/NoteEditor.cpp
    core/src/PitchAnalyzer.cpp
    core/src/PreviewSynth.cpp)

target_include_directories(cutbench_core PUBLIC core/include)
target_compile_features(cutbench_core PUBLIC cxx_std_17)
target_link_libraries(cutbench_core PUBLIC Threads::Threads)
set_target_properties(cutbench_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(cutbench_core PRIVATE /W4)
else()
    target_compile_options(cutbench_core PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------------------
# Tests (no external framework, works offline)
# ------------------------------------------------------------------------------
if(CUTBENCH_BUILD_TESTS)
    enable_testing()

    add_executable(cutbench_tests
        tests/test_main.cpp
        tests/test_pitch_fft.cpp
        tests/test_analysis.cpp
        tests/test_midi.cpp
        tests/test_notes.cpp
        tests/test_engine.cpp
        tests/test_preview.cpp)

    target_include_directories(cutbench_tests PRIVATE tests)
    target_link_libraries(cutbench_tests PRIVATE cutbench_core)
    add_test(NAME cutbench_core_tests COMMAND cutbench_tests)
endif()

# ------------------------------------------------------------------------------
# JUCE plugin
# ------------------------------------------------------------------------------
if(CUTBENCH_BUILD_PLUGIN)
    if(CUTBENCH_JUCE_DIR)
        add_subdirectory("${CUTBENCH_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.4
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(juce)
    endif()

    set(CUTBENCH_FORMATS Standalone VST3)
    if(APPLE)
        list(APPEND CUTBENCH_FORMATS AU)   # AU = the ".component" file
    endif()

    juce_add_plugin(CutbenchNotes
        COMPANY_NAME "Cutbench"
        PRODUCT_NAME "Cutbench Notes"
        PLUGIN_MANUFACTURER_CODE Cbnc
        PLUGIN_CODE Cbnn
        FORMATS ${CUTBENCH_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Analyzer
        AU_MAIN_TYPE kAudioUnitType_Effect
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Cutbench Notes analyses the audio input of the standalone app."
        COPY_PLUGIN_AFTER_BUILD ${CUTBENCH_COPY_PLUGIN})

    target_sources(CutbenchNotes PRIVATE
        plugin/src/AnalysisView.cpp
        plugin/src/PatternEditor.cpp
        plugin/src/PluginEditor.cpp
        plugin/src/PluginProcessor.cpp)

    target_compile_definitions(CutbenchNotes PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(CutbenchNotes
        PRIVATE
            cutbench_core
            juce::juce_audio_utils
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
