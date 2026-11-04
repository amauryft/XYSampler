cmake_minimum_required(VERSION 3.22)

project(HitPad VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Install the built plugin into the user plugin folders after building (macOS/Linux/Windows).
# Disable with -DCOPY_PLUGIN_AFTER_BUILD=OFF (CI does this).
if(NOT DEFINED COPY_PLUGIN_AFTER_BUILD)
    set(COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(HitPad
    COMPANY_NAME               "Amaury"
    PLUGIN_MANUFACTURER_CODE   Amfn
    PLUGIN_CODE                HtPd
    PRODUCT_NAME               "HitPad"
    FORMATS                    AU VST3 Standalone
    IS_SYNTH                   TRUE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD    ${COPY_PLUGIN_AFTER_BUILD})

target_sources(HitPad PRIVATE
    Source/HitCore.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/XYPad.h
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

target_compile_definitions(HitPad PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(HitPad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# DSP unit tests (no JUCE needed):  cmake --build build --target hit_core_test && ctest
add_executable(hit_core_test EXCLUDE_FROM_ALL tests/core_test.cpp)
target_include_directories(hit_core_test PRIVATE Source)
enable_testing()
add_test(NAME core COMMAND hit_core_test)
