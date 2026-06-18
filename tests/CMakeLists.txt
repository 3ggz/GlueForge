# Unit-test target. Uses JUCE's console-app scaffolding so JUCE modules build
# correctly, with Catch2 providing main(). DSP-correctness tests grow from
# Phase 2 onward; Phase 1 covers the gain signal path and state round-trip.

juce_add_console_app(GlueForgeTests PRODUCT_NAME "GlueForgeTests")

target_sources(GlueForgeTests PRIVATE
    test_main.cpp
    ${CMAKE_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/PluginEditor.cpp)

target_include_directories(GlueForgeTests PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(GlueForgeTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(GlueForgeTests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

include(Catch)
catch_discover_tests(GlueForgeTests)
