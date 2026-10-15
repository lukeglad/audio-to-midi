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
            while (quartersPerBar > 0.0 && ppqAtStart >= barStartPpq + quartersPerBar - 1.0e-9)
            {
                barStartPpq += quartersPerBar;
                ++startBar;
            }
        }
    };
}
