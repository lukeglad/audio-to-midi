#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioToMidiProcessor::AudioToMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    downloader->addChangeListener (this);
}

AudioToMidiProcessor::~AudioToMidiProcessor()
{
    downloader->removeChangeListener (this);

    if (job != nullptr)
        job->cancel();

    ++loadGeneration;                       // any running decode bails at its next block
    loaderPool.removeAllJobs (true, 10000);
}

//==============================================================================
void AudioToMidiProcessor::prepareToPlay (double, int)
{
}

bool AudioToMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void AudioToMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // For MIDI export: notes convert to ticks at the host's tempo, so a
    // dragged clip lines up with the audio in real time.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);

    // Silent until the preview synth exists (build step 5).
    buffer.clear();
}

//==============================================================================
juce::AudioProcessorEditor* AudioToMidiProcessor::createEditor()
{
    return new AudioToMidiEditor (*this);
}

//==============================================================================
void AudioToMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Only the editor size so far. Notes and the source audio join this in
    // build step 6, at which point the format needs a version attribute.
    juce::ValueTree state ("AudioToMidiState");
    state.setProperty ("editorWidth",  lastEditorWidth,  nullptr);
    state.setProperty ("editorHeight", lastEditorHeight, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AudioToMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType ("AudioToMidiState"))
        {
            lastEditorWidth  = state.getProperty ("editorWidth",  0);
            lastEditorHeight = state.getProperty ("editorHeight", 0);
        }
    }
}

//==============================================================================
void AudioToMidiProcessor::loadFile (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const int generation = ++loadGeneration;

    loadingFileName = file.getFileName();
    loadError.clear();
    sendChangeMessage();

    juce::WeakReference<AudioToMidiProcessor> weakThis (this);

    loaderPool.addJob ([this, weakThis, file, generation]
    {
        // Reading loadGeneration through `this` is safe here: the destructor
        // waits for this job before any member goes away.
        auto decoded = decodeAudioFile (file, [this, generation]
        {
            return loadGeneration.load() != generation;
        });

        juce::MessageManager::callAsync ([weakThis, generation, result = std::move (decoded)]
        {
            if (auto* self = weakThis.get())
                self->finishLoad (generation, result);
        });
    });
}

void AudioToMidiProcessor::finishLoad (int generation, DecodeResult result)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (generation != loadGeneration.load())
        return;   // superseded by a newer drop

    loadingFileName.clear();

    if (result.audio != nullptr)
    {
        source = std::move (result.audio);
        startTranscription();
    }
    else
    {
        loadError = result.error;
    }

    sendChangeMessage();
}

//==============================================================================
void AudioToMidiProcessor::startTranscription()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (job != nullptr)
        job->cancel();
    job.reset();

    const int generation = ++jobGeneration;

    transcription = {};
    transcription.model = atm::preferredModel();

    if (source == nullptr)
    {
        sendChangeMessage();
        return;
    }

    if (! atm::isModelInstalled (transcription.model))
    {
        transcription.phase = TranscriptionPhase::waitingForModel;
        sendChangeMessage();
        return;
    }

    transcription.phase = TranscriptionPhase::queued;

    // Every callback arrives on the worker thread and hops here through a weak
    // reference; a callback from a superseded job is dropped by generation.
    juce::WeakReference<AudioToMidiProcessor> weakThis (this);

    job = std::make_shared<atm::TranscriptionJob>();
    job->source = source;
    job->model = transcription.model;

    job->onProgress = [weakThis, generation] (atm::Phase phase, float progress)
    {
        juce::MessageManager::callAsync ([weakThis, generation, phase, progress]
        {
            if (auto* self = weakThis.get())
                self->onJobProgress (generation, phase, progress);
        });
    };
    job->onNotes = [weakThis, generation] (std::vector<msl::Note> newNotes, double finalizedThrough)
    {
        juce::MessageManager::callAsync ([weakThis, generation, newNotes, finalizedThrough]
        {
            if (auto* self = weakThis.get())
                self->onJobNotes (generation, newNotes, finalizedThrough);
        });
    };
    job->onFinished = [weakThis, generation] (atm::Outcome outcome, juce::String error, juce::String device)
    {
        juce::MessageManager::callAsync ([weakThis, generation, outcome, error, device]
        {
            if (auto* self = weakThis.get())
                self->onJobFinished (generation, outcome, error, device);
        });
    };

    service->submit (job);
    sendChangeMessage();
}

void AudioToMidiProcessor::cancelTranscription()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (job == nullptr || ! transcription.isRunning())
        return;

    job->cancel();
    transcription.phase = TranscriptionPhase::cancelled;
    sendChangeMessage();
}

void AudioToMidiProcessor::chooseModel (atm::ModelSize size)
{
    JUCE_ASSERT_MESSAGE_THREAD

    atm::setPreferredModel (size);

    if (atm::isModelInstalled (size))
    {
        startTranscription();   // also handles "no audio yet"
        return;
    }

    downloader->start (size);

    if (job != nullptr)
        job->cancel();
    job.reset();
    ++jobGeneration;

    transcription = {};
    transcription.model = size;
    transcription.phase = source != nullptr ? TranscriptionPhase::waitingForModel : TranscriptionPhase::none;
    sendChangeMessage();
}

void AudioToMidiProcessor::cancelDownload()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (const auto busy = downloader->busySize())
        downloader->cancel (*busy);
}

void AudioToMidiProcessor::retry()
{
    JUCE_ASSERT_MESSAGE_THREAD

    switch (transcription.phase)
    {
        case TranscriptionPhase::waitingForModel:
            downloader->start (transcription.model);
            sendChangeMessage();
            break;

        case TranscriptionPhase::cancelled:
        case TranscriptionPhase::failed:
            startTranscription();
            break;

        case TranscriptionPhase::none:
        case TranscriptionPhase::queued:
        case TranscriptionPhase::resampling:
        case TranscriptionPhase::loadingModel:
        case TranscriptionPhase::transcribing:
        case TranscriptionPhase::done:
            break;
    }
}

void AudioToMidiProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A download started, finished or failed. The one we were waiting for
    // landing is the cue to transcribe.
    if (transcription.phase == TranscriptionPhase::waitingForModel && atm::isModelInstalled (transcription.model))
        startTranscription();
    else
        sendChangeMessage();
}

//==============================================================================
void AudioToMidiProcessor::onJobProgress (int generation, atm::Phase phase, float progress)
{
    if (generation != jobGeneration || ! transcription.isRunning())
        return;

    switch (phase)
    {
        case atm::Phase::queued:       transcription.phase = TranscriptionPhase::queued;       break;
        case atm::Phase::resampling:   transcription.phase = TranscriptionPhase::resampling;   break;
        case atm::Phase::loadingModel: transcription.phase = TranscriptionPhase::loadingModel; break;
        case atm::Phase::transcribing: transcription.phase = TranscriptionPhase::transcribing; break;
    }

    transcription.progress = progress;
    sendChangeMessage();
}

void AudioToMidiProcessor::onJobNotes (int generation, std::vector<msl::Note> newNotes, double finalizedThrough)
{
    if (generation != jobGeneration || ! transcription.isRunning())
        return;

    transcription.notes.insert (transcription.notes.end(), newNotes.begin(), newNotes.end());
    transcription.finalizedThrough = juce::jmax (transcription.finalizedThrough, finalizedThrough);
    sendChangeMessage();
}

void AudioToMidiProcessor::onJobFinished (int generation, atm::Outcome outcome, juce::String error, juce::String device)
{
    if (generation != jobGeneration)
        return;

    job.reset();
    transcription.device = device;

    switch (outcome)
    {
        case atm::Outcome::finished:
            transcription.phase = TranscriptionPhase::done;
            transcription.progress = 1.0f;
            transcription.finalizedThrough = source != nullptr ? source->getLengthSeconds() : 0.0;
            break;

        case atm::Outcome::cancelled:
            transcription.phase = TranscriptionPhase::cancelled;
            break;

        case atm::Outcome::failed:
            transcription.phase = TranscriptionPhase::failed;
            transcription.error = error;
            break;
    }

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioToMidiProcessor();
}
