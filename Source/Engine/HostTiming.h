#pragma once

#include <cmath>

namespace atm
{
    /**
        Where on the host's timeline a recording began, so its notes can land
        back on the host's grid: the tempo and the position in quarter notes
        of the first sample, plus the bar that sample falls in.
    */
    struct HostTiming
    {
        double bpm = 120.0;
        double ppqAtStart = 0.0;      // the host's position at the first sample, in quarter notes
        double barStartPpq = 0.0;     // the last bar start at or before ppqAtStart
        double quartersPerBar = 4.0;  // from the time signature
        int startBar = 1;             // 1-based number of the bar barStartPpq begins

        /** The same timing `seconds` later, for a recording trimmed at the front. */
        void advance (double seconds) noexcept
        {
            ppqAtStart += seconds * bpm / 60.0;
            settleBar();
        }

        /** Moves the bar start (and its number) to the bar that actually
            holds ppqAtStart. Hosts don't all report the last bar start
            reliably: FL said 0 while playing from bar 4 (2026-10-15). */
        void settleBar() noexcept
        {
            if (quartersPerBar <= 0.0)
                return;

            while (ppqAtStart >= barStartPpq + quartersPerBar - 1.0e-9)
            {
                barStartPpq += quartersPerBar;
                ++startBar;
            }
            while (ppqAtStart < barStartPpq - 1.0e-9)
            {
                barStartPpq -= quartersPerBar;
                --startBar;
            }
        }
    };
}
