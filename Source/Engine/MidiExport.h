#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <muscriptor/note.hpp>

#include "Cleanup.h"
#include "Tempo.h"

#include <optional>
#include <vector>

namespace atm
{
    /** "acoustic guitar", "drums", "program 97". */
    juce::String layerName (int layerKey);

    /** "C4" for 60 (scientific pitch: middle C is C4). */
    juce::String pitchName (int midiNote);

    //==========================================================================
    // MIDI.

    inline constexpr int kTicksPerQuarter = 960;

    /** Shortest note written: a 1/32 note. Drum hits arrive 10 ms long and
        would be invisible slivers in a piano roll. */
    inline constexpr int kMinNoteTicks = kTicksPerQuarter / 8;

    /**
        How seconds become ticks.

        Normally (conform on) the detected grid's beats become the file's
        beats, so the DAW plays the part at its own tempo and on its grid.
        Conform off is only the fallback for when no tempo exists yet: real
        time at the host's tempo, tick 0 at the start of the audio.

        With conform on, A lead-in of whole beats puts the
        first downbeat on a bar line and keeps every note at or after tick 0.
        A file whose notes start on its first downbeat starts at bar 1; only a
        real pickup (notes before that downbeat) pushes it into the next bar.
    */
    struct ExportTiming
    {
        double hostBpm = 120.0;
        bool conform = false;
        TempoEstimate tempo;        // with the user's edits applied
        double earliestOnset = 0.0; // seconds; the first note of the whole transcription

        // A recording: the clip starts at the host bar it was recorded
        // from, whatever the first note is (see leadInBeatsFor).
        std::optional<int> fixedLeadInBeats;

        /** Beats of lead-in before the grid's beat 0, when conforming. */
        int leadInBeats() const noexcept;
    };

    /** The earliest onset in `notes`, or 0 with none. */
    double earliestOnsetOf (const std::vector<MidiNote>& notes) noexcept;

    double tickFor (double seconds, const ExportTiming&);

    /**
        One layer's notes (after cleanup) as a single-track MIDI file. Drums go
        on channel 10, everything else on channel 1 with the GM program the
        model decoded. Same-pitch notes never overlap: a note is cut where the
        next one on its pitch starts.
    */
    juce::MidiFile buildLayerMidi (const std::vector<MidiNote>& allNotes, int layerKey, const ExportTiming&);

    /** Writes `midi` to `file`, replacing it. */
    bool writeMidiFile (const juce::MidiFile& midi, const juce::File& file);

    /** "<audio name> - <layer>.mid", made safe for a file name. */
    juce::String layerMidiFileName (const juce::String& audioFileName, int layerKey);
}
