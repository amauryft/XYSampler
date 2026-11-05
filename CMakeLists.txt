cmake_minimum_required(VERSION 3.22)
project(XYSampler VERSION 0.2.3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED COPY_PLUGIN_AFTER_BUILD)
    set(COPY_PLUGIN_AFTER_BUILD FALSE)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(XYSampler
    COMPANY_NAME               "Mau Audio"
    BUNDLE_ID                  "com.mauaudio.xysampler"
    PLUGIN_MANUFACTURER_CODE   Mauf
    PLUGIN_CODE                XYsm
    PRODUCT_NAME               "XYSampler"
    FORMATS                    AU VST3 Standalone
    IS_SYNTH                   TRUE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER          TRUE
    VST3_CATEGORIES            Instrument Sampler Drum
    COPY_PLUGIN_AFTER_BUILD    ${COPY_PLUGIN_AFTER_BUILD})

juce_add_binary_data(XYSamplerUI SOURCES
    ui/index.html
    ui/fonts/Michroma-Regular.ttf
    ui/fonts/Manrope-Variable.ttf
    ${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/index.js
    ${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/check_native_interop.js)

target_sources(XYSampler PRIVATE
    Source/HitCore.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(XYSampler PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(XYSampler
    PRIVATE
        XYSamplerUI
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(hit_core_test EXCLUDE_FROM_ALL tests/core_test.cpp)
target_include_directories(hit_core_test PRIVATE Source)
