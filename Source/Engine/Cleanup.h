#pragma once

#include <juce_core/juce_core.h>
#include <muscriptor/note.hpp>

#include "Tempo.h"

#include <cmath>
#include <optional>
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

    //==========================================================================
    /** A note as the plugin shows and exports it: the model's note after
        cleanup. Same order as the raw notes it came from. */
    struct MidiNote
    {
        double onset = 0.0, offset = 0.0;   // seconds, real time (the roll's axis)
        int pitch = 60;
        int layer = 0;                      // layerKeyFor() of the raw note
        bool drum = false;
        int velocity = 100;                 // 1..127
    };

    /** Every note exports at this velocity. The model predicts none, and
        measuring it from the audio (Engine/Velocity, now CLI-only research)
        was too rough on real mixes to ship. */
    inline constexpr int kDefaultVelocity = 100;

    //==========================================================================
    // Cleanup.

    enum class Grid { quarter = 1, eighth = 2, sixteenth = 4 };   // value = divisions per beat

    juce::String gridName (Grid);   // "1/16"

    struct CleanupSettings
    {
        double quantize = 0.0;      // 0..1: how far note starts move toward the grid
        Grid grid = Grid::sixteenth;

        bool operator== (const CleanupSettings& o) const noexcept
        {
            return std::abs (quantize - o.quantize) < 1.0e-9 && grid == o.grid;
        }
    };

    /**
        Raw notes in, MidiNotes out, same order and count. Quantize moves each
        start `quantize` of the way to the nearest grid line of `tempo`; the
        note keeps its length. No tempo, no quantize.

        (Chord tightening, key detection / snap to scale and measured
        velocity were all built and then removed. The model
        already writes strums as block chords; its pitch errors are octaves
        and harmonics, not semitones; and velocity from a dense mix was too
        rough to be musical.)
    */
    std::vector<MidiNote> cleanUp (const std::vector<msl::Note>& raw,
                                   const CleanupSettings&,
                                   const std::optional<TempoEstimate>& tempo);
}
