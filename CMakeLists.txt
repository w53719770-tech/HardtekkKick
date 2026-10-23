cmake_minimum_required(VERSION 3.22)
project(HardtekkKick VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

set(COPY_AFTER TRUE)
if(DEFINED ENV{CI})
    set(COPY_AFTER FALSE)
endif()

juce_add_plugin(HardtekkKick
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER}
    PLUGIN_MANUFACTURER_CODE Htkk
    PLUGIN_CODE Hk01
    FORMATS VST3 Standalone
    PRODUCT_NAME "Hardtekk Kick")

target_sources(HardtekkKick PRIVATE Source/PluginProcessor.h Source/PluginProcessor.cpp)

target_compile_definitions(HardtekkKick PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(HardtekkKick PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
