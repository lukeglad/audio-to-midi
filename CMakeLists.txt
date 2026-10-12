cmake_minimum_required (VERSION 3.24)

# Apple Silicon only (decided 2026-10-12, see the brief). Set before project()
# so every target, including the transcription engine, builds for the one
# architecture. The engine refuses a universal build anyway: ggml picks its CPU
# kernels once per build tree.
set (CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)

# Same minimum OS the EQ installer declares. The engine defaults to the same
# floor; set here so it applies before the engine reads it.
set (CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

project (AudioToMidi VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

# The engine's public headers use std::expected and need C++23.
set (CMAKE_CXX_STANDARD 23)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# The same JUCE checkout EQ builds against.
set (JUCE_DIR "$ENV{HOME}/Documents/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory ("${JUCE_DIR}" JUCE)

#===============================================================================
# Transcription engine: muscriptor.cpp (MIT), a ggml port of MuScriptor. Linked
# statically; the first configure fetches ggml, pinned by commit, so it needs
# network access once. With MUSCRIPTOR_METAL_PRECOMPILED (the default) the
# Metal shaders are compiled at build time into .metallib files that each
# bundle must ship -- see muscriptor_add_metal_library below. Compiling them at
# run time instead would cost ~20 s on the first window-open in every host.
add_subdirectory (external/muscriptor.cpp/cpp muscriptor)

# A Debug ggml is unusably slow (a 15 s clip takes minutes), and there is nothing
# of ours to debug inside it. Keep the engine optimised in every configuration;
# only the plugin follows the build type.
foreach (engine_target muscriptor_ggml ggml ggml-base ggml-cpu ggml-metal pffft)
    if (TARGET ${engine_target})
        target_compile_options (${engine_target} PRIVATE -O3 -DNDEBUG)
    endif()
endforeach()

#===============================================================================
# Sources shared by the plugin and the command-line tool. These include JUCE
# modules directly rather than JuceHeader.h, so they compile in both.
set (ENGINE_SOURCES
    Source/Engine/ModelDownloader.cpp
    Source/Engine/ModelFiles.cpp
    Source/Engine/Resampler.cpp
    Source/Engine/TranscriptionService.cpp
    Source/SourceAudio.cpp)

#===============================================================================
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
    ${ENGINE_SOURCES}
    Source/AudioFlowerLookAndFeel.cpp
    Source/Canvas.cpp
    Source/Chip.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
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
        muscriptor_ggml
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Each format bundle ships the precompiled Metal shaders in Contents/Resources.
# Without them Metal silently fails to initialise and the model runs on the CPU,
# several times slower -- the CLI's "device" line and the plugin's status text
# both report the backend so that is visible.
foreach (format AU VST3 Standalone)
    if (TARGET AudioToMidi_${format})
        muscriptor_add_metal_library (AudioToMidi_${format})
    endif()
endforeach()

#===============================================================================
# atm-cli: decode -> resample -> transcribe from a terminal, through the same
# code the plugin uses. For checking the engine and the audio path without a
# host, and for timing.
juce_add_console_app (atm-cli PRODUCT_NAME "atm-cli")

target_sources (atm-cli PRIVATE
    ${ENGINE_SOURCES}
    Source/Cli/atm-cli.cpp)

target_include_directories (atm-cli PRIVATE Source)

target_compile_definitions (atm-cli PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries (atm-cli
    PRIVATE
        muscriptor_ggml
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

muscriptor_add_metal_library (atm-cli)
