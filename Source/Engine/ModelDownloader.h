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
    host sees the same download. Another process downloading the same size
    holds an InterProcessLock; this one then waits on it, reporting that
    process's progress, rather than downloading a second copy.

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
    /** waiting: another process (another plugin window in another host)
        holds the download; the bytes are its part file's, and this one
        finishes when that one installs the model. */
    enum class Phase { idle, waiting, downloading, verifying, failed };

    /** Why a download stopped, in the terms the UI words it. */
    enum class Failure
    {
        none,
        connection,   // couldn't connect, or the connection dropped (the part file is kept)
        server,       // the server answered with an error or something unexpected
        noSpace,      // not enough free space on the disk for what's left
        cantSave,     // the models folder couldn't be written for another reason
        corrupted     // the download failed its digest twice in a row
    };

    struct Status
    {
        Phase phase = Phase::idle;
        juce::int64 downloadedBytes = 0;   // outside downloading: what the part file holds
        juce::int64 totalBytes = 0;
        Failure failure = Failure::none;   // failed only
        juce::String detail;               // failed only; for the log, not the UI
        bool secondCopy = false;           // downloading again after a copy failed its digest

        bool isBusy() const noexcept { return phase == Phase::waiting || phase == Phase::downloading || phase == Phase::verifying; }
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
