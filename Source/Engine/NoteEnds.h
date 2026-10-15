#pragma once

#include <muscriptor/note.hpp>

#include <functional>
#include <span>
#include <vector>

namespace atm
{
    /**
        Holds each pitched note for as long as the audio keeps sounding at
        its pitch. The model learned its note ends from key releases, so it
        closes a ringing chord while it is still plainly audible: a felt
        piano's final chord ended 3 s before its tail did (2026-10-15).

        For every pitched note, the energy at its fundamental is measured in
        20 ms frames (Goertzel on the model's 16 kHz mono) from the model's
        end onward, and the end moves forward while that energy stays within
        20 dB of the note's own peak and does not jump up (a new attack). It
        stops at the next note of the same pitch on the same layer, at the
        end of the audio, and after at most 10 s. Ends only ever move later.
        Drums are left alone.
    */
    std::vector<msl::Note> holdNoteEnds (std::vector<msl::Note> notes, std::span<const float> samples16k,
                                         const std::function<bool()>& shouldAbort = {});
}
