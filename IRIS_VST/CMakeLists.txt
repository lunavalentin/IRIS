cmake_minimum_required(VERSION 3.15)

project(IRIS_VST VERSION 0.0.1)

add_subdirectory(JUCE)

juce_add_plugin(IRIS_V2
    COMPANY_NAME "IRIS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE IRIS
    PLUGIN_CODE Irs2
    FORMATS VST3
    PRODUCT_NAME "IRIS2"
)

juce_generate_juce_header(IRIS_V2)

target_sources(IRIS_V2
    PRIVATE
        Source/V2/PluginProcessor.cpp
        Source/V2/PluginProcessorV2.h
        Source/V2/PluginEditor.cpp
        Source/V2/PluginEditorV2.h
        Source/V2/RoomMapComponent.cpp
        Source/V2/RoomMapComponentV2.h
        Source/V2/ControlPanelComponent.cpp
        Source/V2/ControlPanelComponentV2.h
        Source/V2/IRListComponent.cpp
        Source/V2/IRListComponentV2.h
)

target_compile_definitions(IRIS_V2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(IRIS_V2
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

juce_add_plugin(IRIS3
    COMPANY_NAME "IRIS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE IRIS
    PLUGIN_CODE Irs3
    FORMATS VST3
    PRODUCT_NAME "IRIS3"
)

juce_generate_juce_header(IRIS3)

target_sources(IRIS3
    PRIVATE
        Source/V3/PluginProcessor.cpp
        Source/V3/PluginProcessorV3.h
        Source/V3/PluginEditor.cpp
        Source/V3/PluginEditorV3.h
        Source/V3/RoomMapComponent.cpp
        Source/V3/RoomMapComponentV3.h
        Source/V3/ControlPanelComponentV3.cpp
        Source/V3/ControlPanelComponentV3.h
        Source/V3/IRListComponent.cpp
        Source/V3/IRListComponentV3.h
        Source/V3/WallListComponent.cpp
        Source/V3/WallListComponentV3.h
)

target_compile_definitions(IRIS3
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(IRIS3
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
