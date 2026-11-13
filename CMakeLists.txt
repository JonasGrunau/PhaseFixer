cmake_minimum_required(VERSION 3.24)

# Before project(): on Apple platforms project() creates an empty cache entry
# for the deployment target, and a set(... CACHE ...) after it never applies,
# so the plugin only loaded on the macOS it was built on (releases said minos
# 26.0). FORCE also fills that empty entry in existing build dirs; an explicit
# -DCMAKE_OSX_DEPLOYMENT_TARGET still wins. Check: otool -l <binary> | grep minos
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Release builds pass the tag's version, e.g. -DPHASEFIXER_VERSION=1.2.0.
set(PHASEFIXER_VERSION "0.6.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(PhaseFixer VERSION ${PHASEFIXER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PHASEFIXER_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(PHASEFIXER_BUILD_TESTS "Build the DSP unit tests" ON)
option(PHASEFIXER_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: DSP, analysis and the real-time phase rotator. Plain C++20, no JUCE,
# so tests and the command-line tool build in seconds.
# ---------------------------------------------------------------------------
add_library(phasefixer_core STATIC
    src/core/dsp/Fft.cpp
    src/core/dsp/Hilbert.cpp
    src/core/align/Analyzer.cpp
    src/core/align/Capture.cpp
    src/core/align/CrossSpectrum.cpp
    src/core/align/LiveSpectrum.cpp
    src/core/align/PhaseMap.cpp
    src/core/align/Rotator.cpp
)
target_include_directories(phasefixer_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(phasefixer_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(phasefixer_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(PHASEFIXER_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(phasefixer_assets SOURCES
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-Medium.ttf)

    juce_add_plugin(PhaseFixer
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.phasefixer"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE PhFx
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "PhaseFixer"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "PhaseFixer listens to the track and reference inputs."
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Tools
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${PHASEFIXER_COPY_PLUGIN})

    # Listed once, used by the plugin and the snapshot tool.
    set(PHASEFIXER_UI_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/Controls.cpp
        src/ui/Scope.cpp
        src/ui/PhaseStrip.cpp
        src/ui/MapLane.cpp
        src/ui/ExportKey.cpp)

    target_sources(PhaseFixer PRIVATE ${PHASEFIXER_UI_SOURCES})
    target_include_directories(PhaseFixer PRIVATE src)

    target_compile_definitions(PhaseFixer PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(PhaseFixer
        PRIVATE
            phasefixer_core
            phasefixer_assets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    juce_add_console_app(phasefixer_snapshot PRODUCT_NAME "phasefixer_snapshot")
    target_sources(phasefixer_snapshot PRIVATE tools/Snapshot.cpp ${PHASEFIXER_UI_SOURCES})
    target_include_directories(phasefixer_snapshot PRIVATE src tests)
    target_compile_definitions(phasefixer_snapshot PRIVATE
        JucePlugin_Name="PhaseFixer"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(phasefixer_snapshot PRIVATE
        phasefixer_core phasefixer_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Dev tool: the plugin in a window with built-in material (or two files)
    # playing through it, reference on the sidechain. ./start.sh runs it.
    juce_add_gui_app(phasefixer_demo PRODUCT_NAME "PhaseFixer Demo")
    target_sources(phasefixer_demo PRIVATE tools/Demo.cpp ${PHASEFIXER_UI_SOURCES})
    target_include_directories(phasefixer_demo PRIVATE src tests)
    target_compile_definitions(phasefixer_demo PRIVATE
        JucePlugin_Name="PhaseFixer"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(phasefixer_demo PRIVATE
        phasefixer_core phasefixer_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests and the command-line aligner
# ---------------------------------------------------------------------------
if(PHASEFIXER_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(phasefixer_tests
        tests/TestDsp.cpp
        tests/TestRotator.cpp
        tests/TestAnalyzer.cpp
        tests/TestPhaseMap.cpp)
    target_include_directories(phasefixer_tests PRIVATE tests)
    target_link_libraries(phasefixer_tests PRIVATE phasefixer_core Catch2::Catch2WithMain)

    add_executable(phasefixer_align tools/AlignFiles.cpp)
    target_link_libraries(phasefixer_align PRIVATE phasefixer_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(phasefixer_tests)
endif()
