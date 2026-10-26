cmake_minimum_required(VERSION 3.22)

project(TheChannelStrip VERSION 2.17.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(JUCE_SOURCE_DIR)
    add_subdirectory(${JUCE_SOURCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(TheChannelStrip
    COMPANY_NAME "Clement Costes"
    BUNDLE_ID "com.clemcostes.thechannelstrip"
    PLUGIN_MANUFACTURER_CODE "ClcS"
    PLUGIN_CODE "Tcst"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "TheChannelStrip"
    COPY_PLUGIN_AFTER_BUILD FALSE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

# v2.17: the studio logo embedded as a binary resource so the plugin
# bundle is fully self-contained (no filesystem access at runtime).
juce_add_binary_data(TcsResources SOURCES resources/CostesLogo.png)

target_sources(TheChannelStrip PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/HostedStrip.cpp
    src/LufsMeter.cpp
    src/SpectrumAnalyzer.cpp
    src/GRForwarder.cpp)

target_compile_definitions(TheChannelStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1)

target_link_libraries(TheChannelStrip
    PRIVATE
        TcsResources
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
