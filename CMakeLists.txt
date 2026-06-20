cmake_minimum_required(VERSION 3.22)

project(GlueForge VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(GLUEFORGE_BUILD_TESTS "Build GlueForge unit tests" ON)

# ---------------------------------------------------------------------------
# JUCE (cloned into libs/JUCE, pinned to 8.0.4)
# ---------------------------------------------------------------------------
add_subdirectory(libs/JUCE)

# ---------------------------------------------------------------------------
# Plugin target
#   FORMATS lists AU even though we only *build* VST3 + Standalone on Windows.
#   Keeping AU declared (invariant #1) makes the deferred Mac track a rebuild,
#   not a rewrite. AU is simply skipped by the generator on non-Apple platforms.
# ---------------------------------------------------------------------------
juce_add_plugin(GlueForge
    COMPANY_NAME              "Nightshift Audio"
    PLUGIN_MANUFACTURER_CODE  Nsau
    PLUGIN_CODE               Glf1
    FORMATS                   VST3 Standalone AU
    PRODUCT_NAME              "GlueForge"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_AUTO_MANIFEST        FALSE
    VST3_CATEGORIES           "Fx" "Dynamics"
    AU_MAIN_TYPE              "kAudioUnitType_Effect")

target_sources(GlueForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(GlueForge PRIVATE Source)

# Embedded resources (the user-manual PDF, opened by the in-plugin Manual button).
# Regenerate the PDF with scripts/build-manual.ps1; the committed file is what CI embeds.
juce_add_binary_data(GlueForgeData SOURCES Resources/GlueForgeManual.pdf)

target_compile_definitions(GlueForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(GlueForge
    PRIVATE
        GlueForgeData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GLUEFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/Catch2)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/libs/Catch2/extras")
    add_subdirectory(tests)
endif()
