#include "TranscriptionService.h"
#include "Resampler.h"

namespace atm
{

namespace
{
    Failure classify (msl::Error error)
    {
        switch (error)
        {
            case msl::Error::InvalidCheckpoint:
            case msl::Error::UnsupportedArch:
            case msl::Error::UnsupportedCheckpointVersion:
                return Failure::modelInvalid;
            case msl::Error::FileNotFound:
                return Failure::modelMissing;
            case msl::Error::OutOfMemory:
                return Failure::outOfMemory;
            case msl::Error::DeviceUnavailable:
            case msl::Error::ContextOverflow:
            case msl::Error::Cancelled:
            case msl::Error::InvalidArgument:
            case msl::Error::Internal:
            case msl::Error::InvalidResumePoint:
                break;
        }

        return Failure::other;
    }

    juce::String backendLabel (const msl::Device& device)
    {
        return juce::String (device.backend).toLowerCase();
    }
}

TranscriptionService::TranscriptionService()
    : juce::Thread ("transcription")
{
    // The engine prints nothing on its own. Warnings and up go to JUCE's log
    // (stderr in the standalone and the CLI).
    msl::setLogCallback ([] (msl::LogLevel, std::string_view text)
    {
        juce::Logger::writeToLog ("engine: " + juce::String (std::string (text)).trimEnd());
    }, msl::LogLevel::Warn);

    startThread (juce::Thread::Priority::low);
}

TranscriptionService::~TranscriptionService()
{
    signalThreadShouldExit();

    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        for (auto& job : queue)
            job->cancel();
        if (current != nullptr)
            current->cancel();
    }

    notify();

    // Unbounded: a cancel is honoured within a prefill (~170 ms), and killing
    // the thread mid-Metal-call would be worse than a short wait.
    stopThread (-1);
}

void TranscriptionService::submit (std::shared_ptr<TranscriptionJob> job)
{
    {
        const std::lock_guard<std::mutex> lock (queueMutex);
        queue.push_back (std::move (job));
    }
    notify();
}

void TranscriptionService::run()
{
    while (! threadShouldExit())
    {
        std::shared_ptr<TranscriptionJob> job;

        {
            const std::lock_guard<std::mutex> lock (queueMutex);
            if (! queue.empty())
            {
                job = std::move (queue.front());
                queue.pop_front();
                current = job;
            }
        }

        if (job == nullptr)
        {
            wait (-1);
            continue;
        }

        if (job->cancelled)
        {
            if (job->onFinished)
                job->onFinished (Outcome::cancelled, Failure::none, {});
        }
        else
        {
            runJob (*job);
        }

        const std::lock_guard<std::mutex> lock (queueMutex);
        current.reset();
    }
}

void TranscriptionService::runJob (TranscriptionJob& job)
{
    const auto shouldCancel = [&job] { return job.cancelled.load(); };
    const auto progress     = [&job] (Phase phase, float fraction)
    {
        if (job.onProgress)
            job.onProgress (phase, fraction);
    };
    const auto finish = [&job] (Outcome outcome, Failure failure = Failure::none, juce::String detail = {})
    {
        if (job.onFinished)
            job.onFinished (outcome, failure, std::move (detail));
    };

    // 1. 16 kHz mono for the model.
    progress (Phase::resampling, -1.0f);
    const auto samples = resampleForModel (*job.source, shouldCancel);

    if (job.cancelled)
        return finish (Outcome::cancelled);

    // A first tempo from the audio's opening minute: cheap, and it puts the
    // beat grid on screen before the model has even loaded.
    if (job.onTempo)
    {
        const size_t firstMinute = juce::jmin (samples.size(), (size_t) (60 * msl::Transcriber::SAMPLE_RATE));
        job.onTempo (estimateTempo (std::span<const float> (samples.data(), firstMinute), {}, shouldCancel));
    }

    // 2. Load. The first availableDevices() call in the process initialises
    //    Metal; with precompiled shaders that is under 100 ms.
    progress (Phase::loadingModel, -1.0f);

    const auto& devices = msl::availableDevices();
    msl::LoadOptions loadOptions;
    if (job.forceCpu && ! devices.empty())
        loadOptions.device = devices.size() - 1;   // the CPU is always last
    loadOptions.should_cancel = shouldCancel;
    loadOptions.on_progress   = [&] (float fraction) { progress (Phase::loadingModel, fraction); };

    const auto file = modelFile (job.model);
    auto transcriber = msl::Transcriber::load (toPath (file), std::move (loadOptions));

    if (! transcriber.has_value())
    {
        if (transcriber.error() == msl::Error::Cancelled)
            return finish (Outcome::cancelled);

        const auto detail = juce::String (msl::describe (transcriber.error()));
        juce::Logger::writeToLog ("transcription: failed to load " + file.getFullPathName() + ": " + detail);
        return finish (Outcome::failed, classify (transcriber.error()), detail);
    }

    const auto device = backendLabel (transcriber->device());
    juce::Logger::writeToLog ("transcription: " + juce::String (modelInfo (job.model).name) + " loaded on "
                              + juce::String (transcriber->device().name) + " (" + device + ")");

    if (job.onLoaded)
        job.onLoaded (device);

    // 3. Transcribe, streaming each chunk's notes out as it closes.
    progress (Phase::transcribing, 0.0f);

    msl::TranscribeOptions options;
    options.should_cancel = shouldCancel;
    // Only matters on the CPU fallback. Leave cores for the host's audio
    // threads: ggml would otherwise take every performance core.
    options.n_threads = juce::jmax (2, juce::SystemStats::getNumPhysicalCpus() - 2);

    std::vector<msl::Note> allNotes;   // for the refined tempo estimate

    const msl::NoteCallback callback = [&] (const msl::TranscriptionUpdate& update)
    {
        allNotes.insert (allNotes.end(), update.new_notes.begin(), update.new_notes.end());

        if (job.onNotes && (! update.new_notes.empty() || update.finalized_through > 0.0))
            job.onNotes (std::vector<msl::Note> (update.new_notes.begin(), update.new_notes.end()),
                         update.finalized_through);

        progress (Phase::transcribing, update.progress);
        return ! job.cancelled.load();
    };

    const auto result = transcriber->transcribe (samples, options, callback);

    if (! result.has_value())
    {
        if (result.error() == msl::Error::Cancelled)
            return finish (Outcome::cancelled);

        const auto detail = juce::String (msl::describe (result.error()));
        juce::Logger::writeToLog ("transcription: failed: " + detail);
        return finish (Outcome::failed, classify (result.error()), detail);
    }

    // The refined tempo: the whole file, with the notes sharpening the onsets
    // and deciding the downbeat.
    if (job.onTempo && ! job.cancelled)
        job.onTempo (estimateTempo (samples, allNotes, shouldCancel));

    // Every note was already delivered through the callback; the return value
    // is the same list joined. The model is released when `transcriber` goes.
    finish (Outcome::finished);
}

juce::String instrumentDisplayName (int program)
{
    if (const auto group = msl::instrumentGroupFor (program))
        return juce::String (std::string (msl::instrumentName (*group))).replaceCharacter ('_', ' ');

    return "program " + juce::String (program);
}

} // namespace atm
