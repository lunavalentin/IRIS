cmake_minimum_required(VERSION 3.15)

# --- macOS portability (must come before project()) ---------------------
# Without these, CMake builds only for the host CPU (arm64 on Apple Silicon)
# and stamps the binary with the host macOS version as the minimum OS,
# so the plugin will not load on Intel Macs or on older macOS releases.
# Override on the command line with -DCMAKE_OSX_ARCHITECTURES=... etc.
if (APPLE)
    if (NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

project(IRIS_VST VERSION 4.5.6)

# The local JUCE checkout must have patches/juce-8.0.12-vst3-oversized-blocks.patch applied.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../dev/IRIS_VST/JUCE")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../dev/IRIS_VST/JUCE" JUCE)
elseif (EXISTS "/Users/luna/Documents/Research/Codes/IRIS/dev/IRIS_VST/JUCE")
    add_subdirectory(/Users/luna/Documents/Research/Codes/IRIS/dev/IRIS_VST/JUCE JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        # Guard against hosts that send larger blocks than announced (VST3 wrapper heap overflow).
        PATCH_COMMAND  git apply --whitespace=nowarn "${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-8.0.12-vst3-oversized-blocks.patch"
        UPDATE_DISCONNECTED TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(IRIS4
    COMPANY_NAME "IRIS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE           # MIDI learn from the host's MIDI input (VST3)
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE IRIS
    PLUGIN_CODE Irs4
    AU_MAIN_TYPE kAudioUnitType_Effect   # stay an "aufx" so existing AU sessions keep loading
    FORMATS VST3 AU
    PRODUCT_NAME "IRIS4"
    VERSION "4.5.6"
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
        Source/V4/IrisMidiLearn.cpp
        Source/V4/IrisMidiLearn.h
        Source/V4/MidiLearnControls.h
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
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
