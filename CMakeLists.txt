# ============================================================================
#  SoloTrack Studio — Phase 1 (Stereo pass-through proof of concept)
#
#  Minimal, self-contained CMake script:
#    * Pulls JUCE 7.0.9 via FetchContent (no pre-clone needed)
#    * Defines one GUI app target via juce_add_gui_app
#    * Links the audio + GUI modules we use
#
#  Configure + build (from this folder, VS 2022 + CMake on PATH):
#      cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#      cmake --build build --config Release
# ============================================================================

cmake_minimum_required(VERSION 3.22)

project(SoloTrack
    VERSION 1.0.0
    DESCRIPTION "Offline-first low-latency practice workstation"
    LANGUAGES C CXX)

# ---- C++ standard ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- JUCE (pinned) --------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

FetchContent_MakeAvailable(JUCE)

# ---- Application target ---------------------------------------------------
juce_add_gui_app(solotrack
    PRODUCT_NAME   "SoloTrack Studio"
    BUNDLE_ID      com.solotrack.studio
    COMPANY_NAME   "SoloTrack"
    COMPANY_WEBSITE "https://example.invalid/solotrack"
    VERSION        1.0.0)

# Modules we link against. juce_add_modules pulls each module's own
# dependencies automatically, so we only list the ones we use directly.
juce_add_modules(solotrack
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_gui_extra)

target_sources(solotrack
    PRIVATE
        Main.cpp)

# Generate JuceHeader.h *after* modules are added so it includes the right set.
juce_generate_juce_header(solotrack)

# Trim unused optional subsystems (keeps the build lean + faster to link).
target_compile_definitions(solotrack PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_SUPPORT=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# Apply JUCE's per-platform target properties (icons, bundle layout, etc.)
juce_product_target_properties(solotrack)
