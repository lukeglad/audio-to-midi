#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "ModelFiles.h"

#include <array>
#include <memory>

namespace atm
{

/**
    Downloads checkpoints into the models directory, one background thread per
    size. One per process (juce::SharedResourcePointer), so every instance in a
    host sees the same download; another process downloading the same size is
    kept out by an InterProcessLock.

    A download is written to modelPartFile() and resumes from it with a Range
    request, so a dropped connection or a closed plugin only costs what was
    not written yet. The finished part is checked against the digest in
    ModelFiles.cpp, then renamed into place.

    Design after NeuralNote's ModelDownloader (Apache-2.0). start, cancel and
    status are for the message thread; the ChangeBroadcaster fires there when
    a download starts, finishes or fails (progress is polled).
*/
class ModelDownloader : public juce::ChangeBroadcaster
{
public:
    enum class Phase { idle, downloading, verifying, failed };

    struct Status
    {
        Phase phase = Phase::idle;
        juce::int64 downloadedBytes = 0;   // outside downloading: what the part file holds
        juce::int64 totalBytes = 0;
        juce::String error;                // failed only; user-facing, lowercase

        bool isBusy() const noexcept { return phase == Phase::downloading || phase == Phase::verifying; }
        float progress() const noexcept;
    };

    ModelDownloader();
    ~ModelDownloader() override;

    /** Does nothing while this size is already busy. Resumes a part file if there is one. */
    void start (ModelSize);

    /** Returns without waiting. The part file is kept, so the next start resumes. */
    void cancel (ModelSize);

    Status status (ModelSize) const;

    /** The size being downloaded or verified right now, if any. */
    std::optional<ModelSize> busySize() const;

private:
    class Job;
    std::array<std::unique_ptr<Job>, kAllModelSizes.size()> jobs;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ModelDownloader)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelDownloader)
};

} // namespace atm
