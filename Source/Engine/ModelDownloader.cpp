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

        if (s.phase == Phase::failed)
        {
            const juce::ScopedLock lock (errorLock);
            s.error = errorMessage;
        }

        return s;
    }

private:
    struct Attempt
    {
        juce::String failure;   // empty: the part file is complete
        bool canRetry = true;
    };

    void run() override
    {
        const auto failure = download();

        if (failure.isNotEmpty() && ! threadShouldExit())
        {
            juce::Logger::writeToLog ("model download: " + juce::String (modelInfo (size).name) + ": " + failure);
            const juce::ScopedLock lock (errorLock);
            errorMessage = failure;
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

    /** @return Why it failed, or empty once installed or cancelled. */
    juce::String download()
    {
        const auto& info = modelInfo (size);

        if (isModelInstalled (size))
            return {};

        juce::InterProcessLock processLock ("AudioFlowerModelDownload_" + juce::String (info.name));
        if (! processLock.enter (0))
            return "another plugin is already downloading this model";

        const auto directory = modelsDirectory();
        if (! directory.createDirectory())
            return "couldn't create " + directory.getFullPathName();

        const auto part = modelPartFile (size);
        deleteOtherPartFiles (part);

        size_t failedAttempts = 0;

        while (part.getSize() != info.numBytes)
        {
            const auto sizeBefore = part.getSize();
            const auto attempt = this->attempt (part, info);

            if (threadShouldExit())
                return {};

            if (attempt.failure.isEmpty())
                continue;

            if (! attempt.canRetry)
                return attempt.failure;

            if (part.getSize() > sizeBefore)
                failedAttempts = 0;

            if (failedAttempts == kRetryDelaysMs.size())
                return attempt.failure;

            wait (kRetryDelaysMs[failedAttempts++]);

            if (threadShouldExit())
                return {};
        }

        phase = Phase::verifying;

        const auto digest = juce::SHA256 (part).toHexString();

        if (threadShouldExit())
            return {};

        if (digest != info.sha256)
        {
            part.deleteFile();
            return "the download was corrupted, try again";
        }

        // Replaces a checkpoint of the wrong size, i.e. a file from another release.
        if (! part.moveFileTo (modelFile (size)))
            return "couldn't move the model into " + directory.getFullPathName();

        juce::Logger::writeToLog ("model download: " + juce::String (info.name) + " installed");
        return {};
    }

    /** One request, from wherever the part file ends. */
    Attempt attempt (const juce::File& part, const ModelInfo& info)
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
            return { "couldn't reach huggingface.co" };

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
                return { "unexpected response from huggingface.co" };
            }
        }
        else if (status == 200)
        {
            // The whole file, whether or not a range was asked for.
            if (offset > 0 && ! part.deleteFile())
                return { "couldn't write to " + part.getFullPathName(), false };

            offset = 0;
            downloadedBytes = 0;
        }
        else if (status == 416)
        {
            part.deleteFile();
            return { "the partial download couldn't be resumed" };
        }
        else
        {
            return { "huggingface.co answered http " + juce::String (status), status >= 500 };
        }

        juce::FileOutputStream output (part);
        if (output.failedToOpen())
            return { "couldn't write to " + part.getFullPathName(), false };

        juce::HeapBlock<char> buffer (kChunkBytes);

        while (! threadShouldExit())
        {
            const int numRead = request.read (buffer.get(), kChunkBytes);
            if (numRead <= 0)
                break;

            if (downloadedBytes.load() + numRead > info.numBytes)
                return { "huggingface.co sent more than the expected size", false };

            if (! output.write (buffer.get(), (size_t) numRead))
                return { "couldn't write to " + part.getFullPathName(), false };

            downloadedBytes += numRead;
        }

        output.flush();
        if (output.getStatus().failed())
            return { "couldn't write to " + part.getFullPathName(), false };

        // A connection that drops mid-body ends the stream early rather than reporting an error.
        if (downloadedBytes.load() != info.numBytes)
            return { "the connection dropped" };

        return {};
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

    juce::CriticalSection errorLock;
    juce::String errorMessage;

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
