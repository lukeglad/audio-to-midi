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
    Source/Engine/Cleanup.cpp
    Source/Engine/MidiExport.cpp
    Source/Engine/ModelDownloader.cpp
    Source/Engine/ModelFiles.cpp
    Source/Engine/NoteEnds.cpp
    Source/Engine/Preview.cpp
    Source/Engine/Recorder.cpp
    Source/Engine/Resampler.cpp
    Source/Engine/SimpleSynth.cpp
    Source/Engine/SoundFont.cpp
    Source/Engine/Tempo.cpp
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

    # An effect on a mixer track (decided 2026-10-14; it began as an
    # instrument): the input passes through and can be recorded, and the
    # preview plays on top. The standalone records from the input device.
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Tools
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "The standalone app records from your input device to transcribe it."

    COPY_PLUGIN_AFTER_BUILD     TRUE)

juce_generate_juce_header (AudioToMidi)

# TinySoundFont (external/TinySoundFont, MIT): the preview's soundfont player.
target_include_directories (AudioToMidi PRIVATE external/TinySoundFont)

target_sources (AudioToMidi PRIVATE
    ${ENGINE_SOURCES}
    Source/AudioFlowerLookAndFeel.cpp
    Source/Canvas.cpp
    Source/Chip.cpp
    Source/DragValue.cpp
    Source/Glass.cpp
    Source/GuideOverlay.cpp
    Source/LayerChip.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/QuantizeControl.cpp
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
# atm-ui: renders the real editor to PNGs (default and minimum size, with and
# without the quick guide), so UI changes can be looked at without a host.
juce_add_gui_app (atm-ui PRODUCT_NAME "atm-ui")
juce_generate_juce_header (atm-ui)

target_sources (atm-ui PRIVATE
    ${ENGINE_SOURCES}
    Source/AudioFlowerLookAndFeel.cpp
    Source/Canvas.cpp
    Source/Chip.cpp
    Source/DragValue.cpp
    Source/Glass.cpp
    Source/GuideOverlay.cpp
    Source/LayerChip.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/QuantizeControl.cpp
    Source/TopBar.cpp
    Source/Cli/atm-ui.cpp)

target_include_directories (atm-ui PRIVATE Source external/TinySoundFont)

target_compile_definitions (atm-ui PRIVATE
    JucePlugin_Name="Audio to MIDI"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries (atm-ui
    PRIVATE
        AudioToMidiBinaryData
        muscriptor_ggml
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_cryptography
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

muscriptor_add_metal_library (atm-ui)

#===============================================================================
# atm-cli: decode -> resample -> transcribe from a terminal, through the same
# code the plugin uses. For checking the engine and the audio path without a
# host, and for timing.
juce_add_console_app (atm-cli PRODUCT_NAME "atm-cli")

target_sources (atm-cli PRIVATE
    ${ENGINE_SOURCES}
    Source/Engine/Velocity.cpp   # research only: not part of the plugin
    Source/Cli/atm-cli.cpp)

target_include_directories (atm-cli PRIVATE Source external/TinySoundFont)

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
