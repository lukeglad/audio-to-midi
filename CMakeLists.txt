cmake_minimum_required (VERSION 3.24)

# Apple Silicon only (decided 2026-10-12, see the brief). Set before project()
# so every target, including the transcription engine added later, builds for
# the one architecture.
set (CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)

# Same minimum OS the EQ installer declares.
set (CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

project (AudioToMidi VERSION 0.1.0 LANGUAGES C CXX)

# The transcription engine (muscriptor.cpp) requires C++23 as a public compile
# feature; setting it now means the skeleton is already built the way it will be.
set (CMAKE_CXX_STANDARD 23)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# The same JUCE checkout EQ builds against.
set (JUCE_DIR "$ENV{HOME}/Documents/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory ("${JUCE_DIR}" JUCE)

juce_add_plugin (AudioToMidi
    # Working title. PRODUCT_NAME is safe to change later; PLUGIN_CODE and
    # PLUGIN_MANUFACTURER_CODE are what hosts identify the plugin by, so they
    # must never change once a build is out.
    PRODUCT_NAME                "Audio to MIDI"
    VERSION                     0.1.0
    DESCRIPTION                 "Turns an audio file into DAW-ready MIDI, one clip per instrument"
    COMPANY_NAME                AudioFlower
    COMPANY_WEBSITE             "https://audioflower.art"
    COMPANY_EMAIL               "luke@audioflower.art"
    BUNDLE_ID                   com.AudioFlower.AudioToMIDI
    PLUGIN_MANUFACTURER_CODE    Lgld
    PLUGIN_CODE                 Atm1

    # An instrument (FL's Channel Rack), not an effect: no audio input, and
    # its only audio output will be the preview synth.
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE

    COPY_PLUGIN_AFTER_BUILD     TRUE)

juce_generate_juce_header (AudioToMidi)

target_sources (AudioToMidi PRIVATE
    Source/AudioFlowerLookAndFeel.cpp
    Source/Canvas.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/SourceAudio.cpp
    Source/TopBar.cpp)

juce_add_binary_data (AudioToMidiBinaryData SOURCES
    Resources/IBMPlexMono-Regular.ttf
    Resources/IndieFlower-Regular.ttf)

target_compile_definitions (AudioToMidi PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries (AudioToMidi
    PRIVATE
        AudioToMidiBinaryData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
