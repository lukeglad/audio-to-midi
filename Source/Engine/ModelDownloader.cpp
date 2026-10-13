#include "ModelDownloader.h"

#include <juce_cryptography/juce_cryptography.h>

namespace atm
{

namespace
{
    constexpr int kConnectionTimeoutMs = 30000;
    constexpr int kMaxRedirects = 5;       // Hugging Face redirects every file to its CDN
    constexpr int kChunkBytes = 1 << 20;

    // Delays before the attempts that follow a failed one. Reset whenever an
    // attempt writes something, so a long download survives a few drops.
    constexpr std::array<int, 3> kRetryDelaysMs { 2000, 5000, 10000 };

    // How often a download waiting on another process looks at its part file.
    constexpr int kWaitPollMs = 500;
}

//==============================================================================
class ModelDownloader::Job : public juce::Thread
{
public:
    Job (ModelDownloader& o, ModelSize s)
        : juce::Thread ("model download (" + juce::String (modelInfo (s).name) + ")"), owner (o), size (s)
    {
    }

    ~Job() override
    {
        requestStop();
        // Unbounded: verifying the large checkpoint takes seconds with nothing to interrupt.
        stopThread (-1);
    }

    void begin()
    {
        if (isThreadRunning())
            return;

        downloadedBytes = modelPartFile (size).getSize();
        phase = Phase::downloading;
        owner.sendChangeMessage();

        wait (0);   // consume a notify() left by a stop that found no delay to wake
        startThread();
    }

    void requestStop()
    {
        signalThreadShouldExit();
        notify();

        const juce::ScopedLock lock (streamLock);
        if (stream != nullptr)
            stream->cancel();
    }

    Status getStatus() const
    {
        Status s;
        s.phase = phase.load();
        s.totalBytes = modelInfo (size).numBytes;
        s.downloadedBytes = s.isBusy() ? downloadedBytes.load() : modelPartFile (size).getSize();
        s.secondCopy = secondCopy.load();

        if (s.phase == Phase::failed)
        {
            const juce::ScopedLock lock (errorLock);
            s.failure = failure;
            s.detail = detail;
        }

        return s;
    }

private:
    struct Result
    {
        Failure failure = Failure::none;   // none: the part file is complete (or cancelled)
        juce::String detail;               // for the log
        bool canRetry = true;
    };

    void run() override
    {
        secondCopy = false;
        const auto result = download();

        if (result.failure != Failure::none && ! threadShouldExit())
        {
            juce::Logger::writeToLog ("model download: " + juce::String (modelInfo (size).name) + ": " + result.detail);
            const juce::ScopedLock lock (errorLock);
            failure = result.failure;
            detail = result.detail;
            phase = Phase::failed;
        }
        else
        {
            phase = Phase::idle;
        }

        juce::MessageManager::callAsync ([safeOwner = juce::WeakReference<ModelDownloader> (&owner)]
        {
            if (auto* o = safeOwner.get())
                o->sendChangeMessage();
        });
    }

    /** @return Why it failed; none once installed or cancelled. */
    Result download()
    {
        const auto& info = modelInfo (size);

        if (isModelInstalled (size))
            return {};

        const auto part = modelPartFile (size);

        // Another process is downloading this size: follow its part file
        // until it lets go, which it does once the model is in place (or
        // when it gives up, and then this one carries on from its bytes).
        juce::InterProcessLock processLock ("AudioFlowerModelDownload_" + juce::String (info.name));
        if (! processLock.enter (0))
        {
            phase = Phase::waiting;
            owner.sendChangeMessage();   // async, and safe from any thread

            while (! processLock.enter (0))
            {
                downloadedBytes = part.getSize();
                wait (kWaitPollMs);

                if (threadShouldExit())
                    return {};
            }

            if (isModelInstalled (size))
                return {};

            phase = Phase::downloading;
            owner.sendChangeMessage();   // async, and safe from any thread
        }

        const auto directory = modelsDirectory();
        if (! directory.createDirectory())
            return writeFailure (directory, info.numBytes);

        deleteOtherPartFiles (part);

        // A copy that fails its digest is fetched again once, by itself;
        // twice in a row means something is wrong that another try won't fix.
        for (int copy = 0;; ++copy)
        {
            if (const auto fetched = fetch (part, info); fetched.failure != Failure::none || threadShouldExit())
                return fetched;

            phase = Phase::verifying;

            const auto digest = juce::SHA256 (part).toHexString();

            if (threadShouldExit())
                return {};

            if (digest == info.sha256)
                break;

            part.deleteFile();

            if (copy == 1)
                return { Failure::corrupted, "the download failed its digest twice" };

            juce::Logger::writeToLog ("model download: " + juce::String (info.name) + ": digest mismatch, downloading again");
            secondCopy = true;
            downloadedBytes = 0;
            phase = Phase::downloading;
            owner.sendChangeMessage();   // async, and safe from any thread
        }

        // Replaces a checkpoint of the wrong size, i.e. a file from another release.
        if (! part.moveFileTo (modelFile (size)))
            return { Failure::cantSave, "couldn't move the model into " + directory.getFullPathName() };

        juce::Logger::writeToLog ("model download: " + juce::String (info.name) + " installed");
        return {};
    }

    /** Until the part file is whole, with a few spaced retries. */
    Result fetch (const juce::File& part, const ModelInfo& info)
    {
        size_t failedAttempts = 0;

        while (part.getSize() != info.numBytes)
        {
            if (part.getParentDirectory().getBytesFreeOnVolume() < info.numBytes - part.getSize())
                return { Failure::noSpace, "not enough free space for " + juce::String (info.name) };

            const auto sizeBefore = part.getSize();
            const auto attempt = this->attempt (part, info);

            if (threadShouldExit())
                return {};

            if (attempt.failure == Failure::none)
                continue;

            if (! attempt.canRetry)
                return attempt;

            if (part.getSize() > sizeBefore)
                failedAttempts = 0;

            if (failedAttempts == kRetryDelaysMs.size())
                return attempt;

            wait (kRetryDelaysMs[failedAttempts++]);

            if (threadShouldExit())
                return {};
        }

        return {};
    }

    /** One request, from wherever the part file ends. */
    Result attempt (const juce::File& part, const ModelInfo& info)
    {
        juce::int64 offset = part.getSize();

        if (offset > info.numBytes)
        {
            part.deleteFile();
            offset = 0;
        }

        downloadedBytes = offset;

        juce::WebInputStream request (modelUrl (size), false);
        request.withConnectionTimeout (kConnectionTimeoutMs).withNumRedirectsToFollow (kMaxRedirects);

        if (offset > 0)
            request.withExtraHeaders ("Range: bytes=" + juce::String (offset) + "-");

        {
            const juce::ScopedLock lock (streamLock);
            stream = &request;
        }

        const juce::ScopeGuard forgetStream { [this]
        {
            const juce::ScopedLock lock (streamLock);
            stream = nullptr;
        }};

        if (threadShouldExit())
            return {};

        if (! request.connect (nullptr))
            return { Failure::connection, "couldn't reach huggingface.co" };

        const int status = request.getStatusCode();
        const auto contentRange = request.getResponseHeaders()["Content-Range"];

        if (status == 206)
        {
            // "bytes <first>-<last>/<total>"
            const bool rangeMatches = contentRange.startsWith ("bytes " + juce::String (offset) + "-")
                && contentRange.fromLastOccurrenceOf ("/", false, false).getLargeIntValue() == info.numBytes;

            if (! rangeMatches)
            {
                part.deleteFile();
                return { Failure::server, "unexpected response from huggingface.co" };
            }
        }
        else if (status == 200)
        {
            // The whole file, whether or not a range was asked for.
            if (offset > 0 && ! part.deleteFile())
                return writeFailure (part, info.numBytes);

            offset = 0;
            downloadedBytes = 0;
        }
        else if (status == 416)
        {
            part.deleteFile();
            return { Failure::server, "the partial download couldn't be resumed" };
        }
        else
        {
            return { Failure::server, "huggingface.co answered http " + juce::String (status), status >= 500 };
        }

        juce::FileOutputStream output (part);
        if (output.failedToOpen())
            return writeFailure (part, info.numBytes - offset);

        juce::HeapBlock<char> buffer (kChunkBytes);

        while (! threadShouldExit())
        {
            const int numRead = request.read (buffer.get(), kChunkBytes);
            if (numRead <= 0)
                break;

            if (downloadedBytes.load() + numRead > info.numBytes)
                return { Failure::server, "huggingface.co sent more than the expected size", false };

            if (! output.write (buffer.get(), (size_t) numRead))
                return writeFailure (part, info.numBytes - downloadedBytes.load());

            downloadedBytes += numRead;
        }

        output.flush();
        if (output.getStatus().failed())
            return writeFailure (part, info.numBytes - downloadedBytes.load());

        // A connection that drops mid-body ends the stream early rather than reporting an error.
        if (downloadedBytes.load() != info.numBytes)
            return { Failure::connection, "the connection dropped" };

        return {};
    }

    /** A write that failed: a full disk, or something else. Not retried. */
    static Result writeFailure (const juce::File& file, juce::int64 bytesStillNeeded)
    {
        const bool full = file.getParentDirectory().getBytesFreeOnVolume() < bytesStillNeeded;
        return { full ? Failure::noSpace : Failure::cantSave, "couldn't write to " + file.getFullPathName(), false };
    }

    /** A part named for another digest was started by a build pinned to other weights. */
    static void deleteOtherPartFiles (const juce::File& part)
    {
        const auto pattern = part.getFileName().upToFirstOccurrenceOf (".gguf", true, false) + ".*.part";

        for (const auto& file : part.getParentDirectory().findChildFiles (juce::File::findFiles, false, pattern))
            if (file != part)
                file.deleteFile();
    }

    ModelDownloader& owner;
    const ModelSize size;

    std::atomic<Phase> phase { Phase::idle };
    std::atomic<juce::int64> downloadedBytes { 0 };

    std::atomic<bool> secondCopy { false };

    juce::CriticalSection errorLock;
    Failure failure = Failure::none;
    juce::String detail;

    // The request in flight, for requestStop to cancel from another thread.
    juce::CriticalSection streamLock;
    juce::WebInputStream* stream = nullptr;
};

//==============================================================================
float ModelDownloader::Status::progress() const noexcept
{
    if (totalBytes <= 0)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, (float) ((double) downloadedBytes / (double) totalBytes));
}

ModelDownloader::ModelDownloader()
{
    for (const auto size : kAllModelSizes)
        jobs[(size_t) size] = std::make_unique<Job> (*this, size);
}

ModelDownloader::~ModelDownloader()
{
    // All at once, so they wind down together rather than one after the other.
    for (auto& job : jobs)
        job->requestStop();

    // Before the jobs' threads are joined: a run() that finishes during the
    // join must not post to a half-destroyed object.
    masterReference.clear();
}

void ModelDownloader::start (ModelSize size)
{
    jobs[(size_t) size]->begin();
}

void ModelDownloader::cancel (ModelSize size)
{
    jobs[(size_t) size]->requestStop();
}

ModelDownloader::Status ModelDownloader::status (ModelSize size) const
{
    return jobs[(size_t) size]->getStatus();
}

std::optional<ModelSize> ModelDownloader::busySize() const
{
    for (const auto size : kAllModelSizes)
        if (status (size).isBusy())
            return size;

    return std::nullopt;
}

} // namespace atm
