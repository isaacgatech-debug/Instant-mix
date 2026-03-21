cmake_minimum_required(VERSION 3.15)

project(InstantMix VERSION 1.0.0)

# Option to build Pro version (6 instruments vs 3 in Base version)
option(INSTANT_MIX_PRO "Build Pro version with 6 instrument types" OFF)

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
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(InstantMix
    PLUGIN_MANUFACTURER_CODE IMix
    PLUGIN_CODE IMix
    FORMATS VST3 AU
    PRODUCT_NAME "Instant Mix"
    COMPANY_NAME "Instant Mix"
    BUNDLE_ID "com.instantmix.plugin"
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
)

target_sources(InstantMix
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/InstantMixLookAndFeel.cpp
        Source/InstantMixLookAndFeel.h
)

target_link_libraries(InstantMix
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
target_include_directories(InstantMix PRIVATE ${juce_SOURCE_DIR}/modules)

# Define INSTANT_MIX_PRO for Pro version
if(INSTANT_MIX_PRO)
    target_compile_definitions(InstantMix
        PUBLIC
            INSTANT_MIX_PRO=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )
    message(STATUS "Building Instant Mix PRO version (6 instruments)")
else()
    target_compile_definitions(InstantMix
        PUBLIC
            INSTANT_MIX_PRO=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )
    message(STATUS "Building Instant Mix BASE version (3 instruments)")
endif()

# Build AudioPluginHost for testing
file(GLOB APH_SOURCES 
    ${juce_SOURCE_DIR}/extras/AudioPluginHost/Source/*.cpp
    ${juce_SOURCE_DIR}/extras/AudioPluginHost/Source/**/*.cpp
)
add_executable(AudioPluginHost ${APH_SOURCES})
target_link_libraries(AudioPluginHost PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
)
target_include_directories(AudioPluginHost PRIVATE 
    ${juce_SOURCE_DIR}/extras/AudioPluginHost/Source
    ${juce_SOURCE_DIR}/modules
)
