cmake_minimum_required(VERSION 3.22)
project(UPFRONT VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the plugin runs without installing the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use a local JUCE checkout if given (-DJUCE_DIR=path), otherwise download it
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(UPFRONT
    COMPANY_NAME "UPFRONT Audio"
    PRODUCT_NAME "UPFRONT"
    PLUGIN_MANUFACTURER_CODE Upfr
    PLUGIN_CODE Upf1
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(UPFRONT PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analyzer.cpp)

target_compile_definitions(UPFRONT PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(UPFRONT
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
