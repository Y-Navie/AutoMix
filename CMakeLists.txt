cmake_minimum_required(VERSION 3.22)
project(GenreAutoMix VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

# Use a local JUCE checkout if present (./JUCE), otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(GenreAutoMix
    COMPANY_NAME "Yaniv"
    BUNDLE_ID "com.yaniv.genreautomix"
    PLUGIN_MANUFACTURER_CODE Ynav
    PLUGIN_CODE GaMx
    FORMATS VST3 AU
    PRODUCT_NAME "Genre AutoMix"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics EQ)

target_sources(GenreAutoMix PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(GenreAutoMix PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GenreAutoMix
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional headless test: cmake -DGAM_TESTS=ON ----
option(GAM_TESTS "Build headless test" OFF)
if(GAM_TESTS)
    juce_add_console_app(GamTest PRODUCT_NAME "GamTest")
    target_sources(GamTest PRIVATE Tests/Test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(GamTest PRIVATE JucePlugin_Name="Genre AutoMix" JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(GamTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
endif()
