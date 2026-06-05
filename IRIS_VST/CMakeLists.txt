cmake_minimum_required(VERSION 3.15)

project(IRIS_VST VERSION 4.2.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(IRIS4
    COMPANY_NAME "IRIS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE IRIS
    PLUGIN_CODE Irs4
    FORMATS VST3 AU
    PRODUCT_NAME "IRIS4"
    VERSION "4.2.0"
)

juce_generate_juce_header(IRIS4)

target_sources(IRIS4
    PRIVATE
        Source/V4/PluginProcessor.cpp
        Source/V4/PluginProcessor.h
        Source/V4/PluginEditor.cpp
        Source/V4/PluginEditor.h
        Source/V4/RoomMapComponent.cpp
        Source/V4/RoomMapComponent.h
        Source/V4/ControlPanelComponent.cpp
        Source/V4/ControlPanelComponent.h
        Source/V4/IRListComponent.cpp
        Source/V4/IRListComponent.h
        Source/V4/ListenerListComponent.cpp
        Source/V4/ListenerListComponent.h
        Source/V4/WallListComponent.cpp
        Source/V4/WallListComponent.h
        Source/V4/IrisOSCManager.cpp
        Source/V4/IrisOSCManager.h
        Source/V4/Theme.cpp
        Source/V4/Theme.h
        Source/V4/IrisLookAndFeel.cpp
        Source/V4/IrisLookAndFeel.h
        Source/V4/IconButton.h
)

target_compile_definitions(IRIS4
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(IRIS4
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_osc
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
