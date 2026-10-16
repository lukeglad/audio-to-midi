#pragma once

#include <muscriptor/note.hpp>

#include "HostTiming.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace atm
{
    /**
        One steady tempo for the whole file (the v1 decision: no drift-following
        tempo map), plus where its beats fall.
    */
    struct TempoEstimate
    {
        double bpm = 120.0;
        double firstBeat = 0.0;     // seconds; beats are at firstBeat + k * 60/bpm for every integer k
        int beatsPerBar = 4;
        int downbeat = 0;           // which beat (0..beatsPerBar-1, counted from firstBeat) starts the first numbered bar
        float confidence = 0.0f;    // 0..1: how much the audio's rhythm agrees with this tempo
        int firstBarNumber = 1;     // the number of that bar: 1 for a file, the host's bar for a recording

        double beatSeconds() const noexcept { return 60.0 / bpm; }

        /** The time of beat k (k may be negative). */
        double beatTime (int k) const noexcept { return firstBeat + k * beatSeconds(); }

        /** Fractional beat index of a time, counted from firstBeat. */
        double beatAt (double seconds) const noexcept { return (seconds - firstBeat) / beatSeconds(); }
    };

    /** A tempo named in a file name, the way sample packs name loops:
        "loop_90bpm.wav", "Keys 128 BPM.wav" (`labelled`: say "bpm", so
        trusted) or a bare number token like Splice's "SP_SZ_154_piano.wav"
        (only a candidate: estimateTempo checks it against the audio). */
    struct NamedTempo
    {
        double bpm = 0.0;
        bool labelled = false;
    };
    std::optional<NamedTempo> tempoFromFileName (const juce::String& fileName);

    /**
        Estimates the tempo of 16 kHz mono audio. Onsets are read from the
        signal (spectral flux); the model's notes, if given, sharpen them and
        decide the downbeat. 40-240 BPM, with a prior around 110; a march
        and its half-time reading are both plausible, which is why typing
        half or double the tempo re-places the downbeat (see setBpm).

        `named`, if given, is the file name's tempo: a labelled one is used
        as is; a bare number only when the audio's own pulse is a simple
        multiple or fraction of it (a sparse loop otherwise reads a chord
        per bar as a chord per beat: 39.7 BPM for a 154 BPM felt-piano
        loop). Either way the beats are then fitted to the audio at exactly
        that tempo.

        Blocking (tens of milliseconds for a song). `shouldAbort` is polled;
        an aborted call returns the default estimate with confidence 0.
    */
    TempoEstimate estimateTempo (std::span<const float> samples16k, std::span<const msl::Note> notes,
                                 const std::function<bool()>& shouldAbort = {},
                                 std::optional<NamedTempo> named = std::nullopt);

    /** The host's grid as a recording sees it: beats at the host's quarter
        notes, bars where the host has them, confidence 1. */
    TempoEstimate gridFor (const HostTiming&) noexcept;

    /** For a recording's export: the whole beats between the bar it began
        in and its first beat, so the clip starts at that bar. */
    int leadInBeatsFor (const TempoEstimate& hostGrid) noexcept;
}
