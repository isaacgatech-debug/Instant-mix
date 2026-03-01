cmake_minimum_required(VERSION 3.15)

project(LeviathexInstantMixer VERSION 1.0.0)

# Set macOS architectures for universal binary
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    SOURCE_DIR /Users/isaacmiddendorf/JUCE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(LeviathexInstantMixer
    PLUGIN_MANUFACTURER_CODE Levi
    PLUGIN_CODE Lmix
    FORMATS VST3 AU
    PRODUCT_NAME "Leviathex Instant Mixer"
    COMPANY_NAME "Leviathex Audio"
    BUNDLE_ID "com.leviathex-audio.instant-mixer"
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
)

target_sources(LeviathexInstantMixer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

target_link_libraries(LeviathexInstantMixer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Add JUCE include directories
target_include_directories(LeviathexInstantMixer PRIVATE ${juce_SOURCE_DIR}/modules)

# Enable warnings
target_compile_definitions(LeviathexInstantMixer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
