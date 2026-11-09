cmake_minimum_required(VERSION 3.22)
project(PhoneMonitor VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 配布用：MSVCランタイムを静的リンク
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PhoneMonitor
    COMPANY_NAME              "Takasono"
    PLUGIN_MANUFACTURER_CODE  Tksn
    PLUGIN_CODE               PhMn
    FORMATS                   VST3
    PRODUCT_NAME              "PhoneMonitor"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Tools)

target_sources(PhoneMonitor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StreamServer.cpp
    Source/TunnelManager.cpp
    Source/qrcodegen/qrcodegen.cpp)

target_compile_definitions(PhoneMonitor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if (MSVC)
    target_compile_options(PhoneMonitor PRIVATE /utf-8)
endif()

target_link_libraries(PhoneMonitor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
