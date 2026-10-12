#pragma once

#include "../SourceAudio.h"

#include <functional>
#include <vector>

namespace atm
{
    /** The only rate the model accepts. */
    inline constexpr double kModelSampleRate = 16000.0;

    /**
        The whole file as 16 kHz mono, aligned so that output sample j sits at
        the same instant as input sample j * (rate / 16000): the model's onsets
        land on a 10 ms grid, and a resampler's latency would shift every note.

        Offline and blocking: run it on a worker thread. `shouldAbort` is
        polled between blocks; an aborted call returns an empty vector.
    */
    std::vector<float> resampleForModel (const SourceAudio&, const std::function<bool()>& shouldAbort = {});
}
