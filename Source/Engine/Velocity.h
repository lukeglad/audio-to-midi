#pragma once

#include <muscriptor/note.hpp>

#include "../SourceAudio.h"

#include <functional>
#include <vector>

//==============================================================================
// Velocity measurement: research code, used by atm-cli only. It was wired
// into the plugin in build step 4b and taken out again the same day: on real
// mixes the per-note levels were too rough to sound musical, and flat
// velocities are at least predictable. Kept so a better model has somewhere
// to start (`atm-cli --velocity-check` still exercises it).
namespace atm
{
    /**
        How loud each note is in the original recording, in dB (arbitrary
        reference; only differences matter). One value per note, same order.

        Pitched notes: energy at the note's fundamental and next two
        harmonics over its first 60 ms, so overlapping instruments on other
        pitches mostly don't count (they do on shared pitches). Drums:
        broadband RMS over the first 30 ms of the hit.
    */
    std::vector<float> measureLevels (const SourceAudio&, const std::vector<msl::Note>&,
                                      const std::function<bool()>& shouldAbort = {});

    /**
        Levels to MIDI velocities, per layer: a layer's quiet end (5th
        percentile) maps to 40 and its loud end (95th) to 120, so a quiet
        instrument under a loud one still spans the range. A layer with no
        real spread (under 0.5 dB) sits at 80.
    */
    std::vector<int> velocitiesFromLevels (const std::vector<msl::Note>&, const std::vector<float>& levels);
}
