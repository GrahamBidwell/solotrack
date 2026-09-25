cmake_minimum_required(VERSION 3.22)

project(SoloTrack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

juce_add_gui_app(SoloTrack
    PRODUCT_NAME "SoloTrack"
    COMPANY_NAME "SoloTrack"
)

target_sources(SoloTrack PRIVATE
    Source/Main.cpp
)

target_link_libraries(SoloTrack PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(SoloTrack)
