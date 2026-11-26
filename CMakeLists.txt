cmake_minimum_required(VERSION 3.22)
project(Nebula2Modulation VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to the JUCE folder")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(Nebula2Modulation
    COMPANY_NAME "Back2You"
    PLUGIN_MANUFACTURER_CODE B2Yu
    PLUGIN_CODE N2Md
    PRODUCT_NAME "Nebula2 Modulation"
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

# Inter fonts from the ParaEQ design system (SIL Open Font License)
juce_add_binary_data(N2MData SOURCES
    Resources/Inter-Regular.ttf
    Resources/Inter-Medium.ttf
    Resources/Inter-SemiBold.ttf)

set(N2M_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PluginHost.cpp)
target_sources(Nebula2Modulation PRIVATE ${N2M_SOURCES})
target_include_directories(Nebula2Modulation PRIVATE Source)

target_compile_definitions(Nebula2Modulation PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Nebula2Modulation
    PRIVATE
        N2MData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Plain C++ test of the rack engine (no JUCE needed)
add_executable(EngineTest tests/EngineTest.cpp)
target_include_directories(EngineTest PRIVATE Source)
find_package(Threads REQUIRED)
target_link_libraries(EngineTest PRIVATE Threads::Threads)

# OfflineTest: the plugin playing in a fake Cubase. Snapshot: PNGs of the window. (No Cubase needed)
foreach(app OfflineTest Snapshot)
    juce_add_console_app(${app} PRODUCT_NAME "${app}")
    target_sources(${app} PRIVATE tests/${app}.cpp ${N2M_SOURCES})
    target_include_directories(${app} PRIVATE Source)
    target_compile_definitions(${app} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Nebula2 Modulation")
    target_link_libraries(${app} PRIVATE N2MData juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)
endforeach()
