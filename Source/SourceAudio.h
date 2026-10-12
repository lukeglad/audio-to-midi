#pragma once

// Module headers rather than JuceHeader.h: this file is shared with atm-cli.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
/**
    A min/max summary of the source audio, for drawing the waveform band.
    Computed once at load, off the message thread, so painting never has to
    touch the full sample data.
*/
struct WaveformSummary
{
    // One entry per bucket, of the mono mix. Paired: mins[i] <= maxs[i].
    std::vector<float> mins, maxs;

    // Largest absolute sample. The canvas normalises by it, because phone
    // memos are often recorded quietly and would otherwise draw as a sliver.
    float peak = 0.0f;
};

//==============================================================================
/**
    The dropped file, decoded. Kept at its original sample rate and channel
    count (up to stereo): the engine gets its own 16 kHz mono copy later, but
    playback and project storage want the original.

    Immutable once built and shared by pointer, so the editor can hold on to
    one while a newer file is still decoding.
*/
struct SourceAudio
{
    juce::String fileName;
    double sampleRate = 0.0;
    juce::AudioBuffer<float> samples;
    WaveformSummary waveform;

    double getLengthSeconds() const noexcept
    {
        return sampleRate > 0.0 ? (double) samples.getNumSamples() / sampleRate : 0.0;
    }
};

struct DecodeResult
{
    std::shared_ptr<const SourceAudio> audio;   // null on failure
    juce::String error;                          // user-facing, lowercase; empty when aborted

    // What the plugin keeps in its saved state so the audio comes back
    // without the file: the file's own bytes when it is already a compressed
    // container (m4a, mp3, ogg, flac), else a 16-bit FLAC of the decoded
    // audio -- a 3-minute wav drop would otherwise put ~60 MB into every
    // project save. Null when encoding failed; the state then holds no audio.
    std::shared_ptr<const juce::MemoryBlock> stateBytes;
    juce::String stateExtension;                 // "m4a", "flac", ...
};

/** Extensions the drop target accepts: the brief's list, plus the obvious
    spellings of the same containers. m4a/aac/caf decode through macOS's own
    decoder (CoreAudioFormat), which is what reads iPhone voice memos. */
bool isSupportedAudioFile (const juce::File&);

/** The supported formats as shown on the empty canvas. */
juce::String supportedFormatsLabel();

/** Decodes a whole file, and prepares its state bytes. Blocking: call it
    from a background thread. `shouldAbort` is polled between blocks, so a
    newer drop can cut a long decode short. */
DecodeResult decodeAudioFile (const juce::File&, const std::function<bool()>& shouldAbort);

/** Decodes state bytes written by an earlier session (`extension` says
    what they are). The same bytes come back as the result's stateBytes. */
DecodeResult decodeAudioData (std::shared_ptr<const juce::MemoryBlock> bytes, const juce::String& extension,
                              const juce::String& fileName, const std::function<bool()>& shouldAbort);
