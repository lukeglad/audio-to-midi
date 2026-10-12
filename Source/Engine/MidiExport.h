#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <muscriptor/note.hpp>

#include <vector>

namespace atm
{
    //==========================================================================
    // Layers: one per instrument the model reported. Drums are one layer
    // whatever program they decoded as (128, or 96 routed to drums).

    inline constexpr int kDrumLayer = msl::DRUM_PROGRAM;

    inline int layerKeyFor (const msl::Note& note) noexcept
    {
        return note.is_drum ? kDrumLayer : note.program;
    }

    /** "acoustic guitar", "drums", "program 97". */
    juce::String layerName (int layerKey);

    /** "C4" for 60 (scientific pitch: middle C is C4). */
    juce::String pitchName (int midiNote);

    //==========================================================================
    // MIDI.

    /** Shortest note written, in ticks at 960 per quarter: a 1/32 note. Drum
        hits arrive 10 ms long and would be invisible slivers in a piano roll. */
    inline constexpr int kTicksPerQuarter = 960;
    inline constexpr int kMinNoteTicks = kTicksPerQuarter / 8;

    /** Until velocity measurement exists (build step 4). */
    inline constexpr juce::uint8 kFixedVelocity = 100;

    /**
        One layer's notes as a single-track MIDI file. Seconds convert to ticks
        at `bpm`, so with the host's tempo the clip lines up with the original
        audio in real time; tick 0 is the start of the audio (leading silence
        is kept). Drums go on channel 10, everything else on channel 1 with
        the GM program the model decoded. Same-pitch notes never overlap: a
        note is cut where the next one on its pitch starts.
    */
    juce::MidiFile buildLayerMidi (const std::vector<msl::Note>& allNotes, int layerKey, double bpm);

    /** Writes `midi` to `file`, replacing it. */
    bool writeMidiFile (const juce::MidiFile& midi, const juce::File& file);

    /** "<audio name> - <layer>.mid", made safe for a file name. */
    juce::String layerMidiFileName (const juce::String& audioFileName, int layerKey);
}
