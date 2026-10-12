#pragma once

#include <juce_core/juce_core.h>
#include <muscriptor/muscriptor.hpp>

#include "../SourceAudio.h"
#include "ModelFiles.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace atm
{

enum class Phase   { queued, resampling, loadingModel, transcribing };
enum class Outcome { finished, cancelled, failed };

/**
    One transcription: the audio to run, which model, and where to send what
    comes back. Owned by whoever submitted it (shared with the service only
    while it runs), so cancelling is a call on the job itself.

    The callbacks run on the service's worker thread, in order, and never after
    onFinished. A UI hops them onto the message thread itself; the CLI consumes
    them directly.
*/
struct TranscriptionJob
{
    std::shared_ptr<const SourceAudio> source;
    ModelSize model = kDefaultModelSize;
    bool forceCpu = false;

    std::function<void (Phase, float progress)> onProgress;

    /** Notes finalised since the last call, and the time below which the
        transcription is now complete. Each note arrives exactly once. */
    std::function<void (std::vector<msl::Note> newNotes, double finalizedThrough)> onNotes;

    /** `device` is the backend the model ran on ("metal" or "cpu"); `error`
        is user-facing, lowercase, empty unless failed. */
    std::function<void (Outcome, juce::String error, juce::String device)> onFinished;

    void cancel() noexcept { cancelled = true; }
    std::atomic<bool> cancelled { false };
};

/**
    Runs transcriptions one at a time on a single worker thread. One per
    process (hold it through juce::SharedResourcePointer): two instances
    transcribing at once would each take the whole GPU and run slower than in
    sequence, and ggml's device list is per process anyway.

    The model is loaded for each job and released after it. Loading takes a
    quarter of a second at medium, while holding a gigabyte of weights and KV
    cache in a DAW that may not transcribe again for hours is not worth it.
*/
class TranscriptionService : private juce::Thread
{
public:
    TranscriptionService();
    ~TranscriptionService() override;

    /** Queues the job. Thread-safe. */
    void submit (std::shared_ptr<TranscriptionJob>);

private:
    void run() override;
    void runJob (TranscriptionJob&);

    std::mutex queueMutex;
    std::deque<std::shared_ptr<TranscriptionJob>> queue;
    std::shared_ptr<TranscriptionJob> current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriptionService)
};

/** "acoustic_guitar" -> "acoustic guitar"; unnamed groups become "program 97". */
juce::String instrumentDisplayName (int program);

} // namespace atm
