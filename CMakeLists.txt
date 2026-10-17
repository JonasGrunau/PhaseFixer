cmake_minimum_required(VERSION 3.24)

# Release builds pass the tag's version, e.g. -DPHASEFUCKER_VERSION=1.2.0.
set(PHASEFUCKER_VERSION "0.2.0" CACHE STRING "Plugin version, MAJOR.MINOR.PATCH")
project(PhaseFucker VERSION ${PHASEFUCKER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime, so the Windows plugin doesn't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PHASEFUCKER_BUILD_PLUGIN "Build the VST3/AU plugin (needs JUCE)" ON)
option(PHASEFUCKER_BUILD_TESTS "Build the DSP unit tests" ON)
option(PHASEFUCKER_COPY_PLUGIN "Copy the built plugins into the system plug-in folders" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Core: DSP, analysis and the real-time phase rotator. Plain C++20, no JUCE,
# so tests and the command-line tool build in seconds.
# ---------------------------------------------------------------------------
add_library(phasefucker_core STATIC
    src/core/dsp/Fft.cpp
    src/core/dsp/Hilbert.cpp
    src/core/align/Analyzer.cpp
    src/core/align/Capture.cpp
    src/core/align/CrossSpectrum.cpp
    src/core/align/LiveSpectrum.cpp
    src/core/align/Rotator.cpp
)
target_include_directories(phasefucker_core PUBLIC src/core)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(phasefucker_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
set_target_properties(phasefucker_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(PHASEFUCKER_BUILD_PLUGIN)
    # Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to skip the download.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(phasefucker_assets SOURCES
        resources/fonts/IBMPlexSansCondensed-Medium.ttf
        resources/fonts/IBMPlexSansCondensed-SemiBold.ttf
        resources/fonts/IBMPlexMono-Regular.ttf
        resources/fonts/IBMPlexMono-Medium.ttf)

    juce_add_plugin(PhaseFucker
        COMPANY_NAME "Jonas Grunau"
        BUNDLE_ID "de.grunau.phasefucker"
        PLUGIN_MANUFACTURER_CODE Jgru
        PLUGIN_CODE PhFk
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "PhaseFucker"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "PhaseFucker listens to the track and reference inputs."
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Tools
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${PHASEFUCKER_COPY_PLUGIN})

    # Listed once, used by the plugin and the snapshot tool.
    set(PHASEFUCKER_UI_SOURCES
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/ui/Theme.cpp
        src/ui/Controls.cpp
        src/ui/Scope.cpp
        src/ui/PhaseStrip.cpp)

    target_sources(PhaseFucker PRIVATE ${PHASEFUCKER_UI_SOURCES})
    target_include_directories(PhaseFucker PRIVATE src)

    target_compile_definitions(PhaseFucker PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(PhaseFucker
        PRIVATE
            phasefucker_core
            phasefucker_assets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Dev tool: renders the editor to PNG without a host.
    juce_add_console_app(phasefucker_snapshot PRODUCT_NAME "phasefucker_snapshot")
    target_sources(phasefucker_snapshot PRIVATE tools/Snapshot.cpp ${PHASEFUCKER_UI_SOURCES})
    target_include_directories(phasefucker_snapshot PRIVATE src tests)
    target_compile_definitions(phasefucker_snapshot PRIVATE
        JucePlugin_Name="PhaseFucker"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(phasefucker_snapshot PRIVATE
        phasefucker_core phasefucker_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Tests and the command-line aligner
# ---------------------------------------------------------------------------
if(PHASEFUCKER_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(phasefucker_tests
        tests/TestDsp.cpp
        tests/TestRotator.cpp
        tests/TestAnalyzer.cpp)
    target_include_directories(phasefucker_tests PRIVATE tests)
    target_link_libraries(phasefucker_tests PRIVATE phasefucker_core Catch2::Catch2WithMain)

    add_executable(phasefucker_align tools/AlignFiles.cpp)
    target_link_libraries(phasefucker_align PRIVATE phasefucker_core)

    include(CTest)
    include(Catch)
    catch_discover_tests(phasefucker_tests)
endif()
