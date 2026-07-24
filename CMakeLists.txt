# DEXTRO DELAY — a self-ducking stereo delay (VST3 + Standalone).
#
# Statically linked so a built binary depends only on system libraries: JUCE is
# fetched at configure time (FetchContent) and compiled INTO the plugin, so a
# fresh clone builds with nothing preinstalled beyond CMake and a C++17 compiler.

cmake_minimum_required(VERSION 3.22)

project(DextroDelay VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static MSVC runtime: the plugin must load on Windows machines without the
# VC++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinGW + JUCE 7 quirk: a few JUCE headers (e.g. juce_RenderingHelpers.h) call
# memset/memcpy without <cstring> in scope, which the newer MinGW GCC only sees
# "later in the translation unit" at template-instantiation time. Force-include
# <cstring> for every C++ TU (JUCE's own sources included) so the symbols are in
# scope up front. Guarded to C++ so JUCE's C sources aren't affected.
if(MINGW)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstring>")
endif()

# --- JUCE: fetched and built statically into the plugin --------------------
# NB: these cache variables are prefixed with the exact target name so that
# scripts/build-windows.sh (which passes -D<Target>_JUCE_TAG=7.0.12) matches —
# CMake variable names are case-sensitive.
option(DextroDelay_JUCE_PATH "Path to a local JUCE checkout (empty to fetch)" "")

# JUCE 8 for native builds; the MinGW cross-build needs 7.0.12 (JUCE 8 dropped
# MinGW support). The build scripts pass the right tag per target.
set(DextroDelay_JUCE_TAG "8.0.6" CACHE STRING
    "JUCE git tag to fetch (use 7.0.12 for the MinGW cross-build)")

if(DextroDelay_JUCE_PATH)
    add_subdirectory(${DextroDelay_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${DextroDelay_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(MINGW)
    # The Steinberg manifest helper doesn't cross-compile; moduleinfo.json is
    # optional and hosts scan the binary directly without it.
    set(DEXTRODELAY_PLUGIN_EXTRA_ARGS VST3_AUTO_MANIFEST FALSE)
endif()

# --- The plugin ------------------------------------------------------------
# Audio EFFECT: IS_SYNTH FALSE and an Fx VST3 category, so the DAW exposes it on
# audio tracks / insert slots (a delay).
juce_add_plugin(DextroDelay
    ${DEXTRODELAY_PLUGIN_EXTRA_ARGS}
    COMPANY_NAME              "Sprizzle"
    COMPANY_WEBSITE           "https://sprizzle.io"
    BUNDLE_ID                 "io.sprizzle.dextrodelay"
    PLUGIN_MANUFACTURER_CODE  Sprz
    PLUGIN_CODE               Dxdl
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "DEXTRO DELAY"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Fx" "Delay")

# Embed the logo into the binary so nothing is loaded from disk at runtime.
juce_add_binary_data(DextroDelayAssets
    HEADER_NAME DextroDelayData.h
    NAMESPACE   DextroDelayData
    SOURCES     Assets/Logo.png)

target_sources(DextroDelay PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(DextroDelay PRIVATE Source)

target_compile_definitions(DextroDelay PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DextroDelay
    PRIVATE
        DextroDelayAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MINGW)
    # Fully static so the DLL depends only on system libraries, and big-obj
    # because JUCE unity builds overflow the default COFF section limit.
    target_compile_options(DextroDelay PUBLIC -Wa,-mbig-obj)
    target_link_options(DextroDelay PUBLIC -static)
else()
    target_link_libraries(DextroDelay PUBLIC juce::juce_recommended_lto_flags)
endif()

# --- Optional headless DSP test (no host / no JUCE) ------------------------
# The DuckingDelay engine is dependency-free, so the test is a plain executable
# that renders a WAV through it and asserts the self-ducking behaviour.
# Toggle with -DDEXTRODELAY_BUILD_TESTS=ON.
option(DEXTRODELAY_BUILD_TESTS "Build the offline DSP test" OFF)
if(DEXTRODELAY_BUILD_TESTS)
    add_executable(DextroDelayTest Test/DuckTest.cpp)
    target_compile_features(DextroDelayTest PRIVATE cxx_std_17)
endif()

# --- Optional offscreen UI snapshot (renders the editor to a PNG) ----------
# Native only; used to eyeball the neon look without a DAW. -DDEXTRODELAY_BUILD_SNAPSHOT=ON
option(DEXTRODELAY_BUILD_SNAPSHOT "Build the offscreen UI snapshot tool" OFF)
if(DEXTRODELAY_BUILD_SNAPSHOT)
    juce_add_console_app(DextroDelaySnapshot PRODUCT_NAME "DextroDelaySnapshot")
    target_sources(DextroDelaySnapshot PRIVATE
        Test/Snapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(DextroDelaySnapshot PRIVATE Source)
    target_compile_definitions(DextroDelaySnapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DextroDelaySnapshot PRIVATE
        DextroDelayAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
