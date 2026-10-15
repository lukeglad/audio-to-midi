#pragma once

#include <muscriptor/note.hpp>

#include "HostTiming.h"

#include <functional>
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

    /**
        Estimates the tempo of 16 kHz mono audio. Onsets are read from the
        signal (spectral flux); the model's notes, if given, sharpen them and
        decide the downbeat. 40-240 BPM, with a prior around 110, so a march
        and its half-time reading can both be answered either way: the UI's
        half/double buttons exist for that.

        Blocking (tens of milliseconds for a song). `shouldAbort` is polled;
        an aborted call returns the default estimate with confidence 0.
    */
    TempoEstimate estimateTempo (std::span<const float> samples16k, std::span<const msl::Note> notes,
                                 const std::function<bool()>& shouldAbort = {});

    /** The host's grid as a recording sees it: beats at the host's quarter
        notes, bars where the host has them, confidence 1. */
    TempoEstimate gridFor (const HostTiming&) noexcept;

    /** For a recording's export: the whole beats between the bar it began
        in and its first beat, so the clip starts at that bar. */
    int leadInBeatsFor (const TempoEstimate& hostGrid) noexcept;
}
