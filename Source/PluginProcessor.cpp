#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioToMidiProcessor::AudioToMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    downloader->addChangeListener (this);

    // The preview plays through the soundfont once it has loaded (the first
    // instance in the process starts the load). Listen first, then look: a
    // load finishing in between is caught either way.
    soundFonts->addChangeListener (this);
    if (auto font = soundFonts->get())
        preview.setSoundFont (font);
}

AudioToMidiProcessor::~AudioToMidiProcessor()
{
    downloader->removeChangeListener (this);
    soundFonts->removeChangeListener (this);

    if (job != nullptr)
        job->cancel();

    ++loadGeneration;                       // any running decode bails at its next block
    loaderPool.removeAllJobs (true, 10000);
}

//==============================================================================
void AudioToMidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preview.prepare (sampleRate, samplesPerBlock);
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

    preview.render (buffer);
}

//==============================================================================
juce::AudioProcessorEditor* AudioToMidiProcessor::createEditor()
{
    return new AudioToMidiEditor (*this);
}

//==============================================================================
// State: a binary ValueTree (not XML -- the audio would be base64 and a
// third bigger). Version 2 added the audio, notes, tempo and settings;
// version 1 held only the editor size.

namespace
{
    constexpr int kStateVersion = 2;

    // Notes packed as fixed records, so a song's thousands of notes don't
    // become thousands of XML nodes.
    struct PackedNote { double onset, offset; juce::int32 pitch, program; juce::uint8 drum; juce::uint8 pad[7]; };
    static_assert (sizeof (PackedNote) == 32);

    juce::MemoryBlock packNotes (const std::vector<msl::Note>& notes)
    {
        juce::MemoryBlock block (notes.size() * sizeof (PackedNote), true);
        auto* out = static_cast<PackedNote*> (block.getData());
        for (size_t i = 0; i < notes.size(); ++i)
            out[i] = { notes[i].onset, notes[i].offset, notes[i].pitch, notes[i].program, (juce::uint8) (notes[i].is_drum ? 1 : 0), {} };
        return block;
    }

    std::vector<msl::Note> unpackNotes (const juce::MemoryBlock& block)
    {
        std::vector<msl::Note> notes (block.getSize() / sizeof (PackedNote));
        const auto* in = static_cast<const PackedNote*> (block.getData());
        for (size_t i = 0; i < notes.size(); ++i)
            notes[i] = { in[i].onset, in[i].offset, in[i].pitch, in[i].program, in[i].drum != 0 };
        return notes;
    }

    juce::String joinInts (const std::vector<int>& values)
    {
        juce::StringArray parts;
        for (const int v : values)
            parts.add (juce::String (v));
        return parts.joinIntoString (" ");
    }

    std::vector<int> splitInts (const juce::String& text)
    {
        std::vector<int> values;
        for (const auto& part : juce::StringArray::fromTokens (text, " ", {}))
            if (part.isNotEmpty())
                values.push_back (part.getIntValue());
        return values;
    }
}

void AudioToMidiProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("AudioToMidiState");
    state.setProperty ("version", kStateVersion, nullptr);
    state.setProperty ("editorWidth",  lastEditorWidth,  nullptr);
    state.setProperty ("editorHeight", lastEditorHeight, nullptr);

    if (source != nullptr && stateAudio != nullptr)
    {
        juce::ValueTree audio ("audio");
        audio.setProperty ("name", source->fileName, nullptr);
        audio.setProperty ("extension", stateAudioExtension, nullptr);
        audio.setProperty ("bytes", juce::var (*stateAudio), nullptr);
        state.appendChild (audio, nullptr);

        juce::ValueTree notes ("notes");
        notes.setProperty ("complete", transcription.phase == TranscriptionPhase::done, nullptr);
        notes.setProperty ("packed", juce::var (packNotes (transcription.notes)), nullptr);
        state.appendChild (notes, nullptr);

        juce::ValueTree t ("tempo");
        t.setProperty ("hasEstimate", tempo.hasEstimate, nullptr);
        t.setProperty ("bpm", tempo.bpm, nullptr);
        t.setProperty ("userEdited", tempo.userEdited, nullptr);
        t.setProperty ("detectedBpm", tempo.detected.bpm, nullptr);
        t.setProperty ("firstBeat", tempo.detected.firstBeat, nullptr);
        t.setProperty ("beatsPerBar", tempo.detected.beatsPerBar, nullptr);
        t.setProperty ("downbeat", tempo.detected.downbeat, nullptr);
        t.setProperty ("confidence", (double) tempo.detected.confidence, nullptr);
        state.appendChild (t, nullptr);
    }

    juce::ValueTree c ("cleanup");
    c.setProperty ("quantize", cleanup.quantize, nullptr);
    c.setProperty ("grid", (int) cleanup.grid, nullptr);
    state.appendChild (c, nullptr);

    juce::ValueTree p ("preview");
    p.setProperty ("mix", (double) preview.getMix(), nullptr);
    std::vector<int> muted, soloed;
    for (int layer = 0; layer < atm::Preview::kLayerSlots; ++layer)
    {
        if (preview.isMuted (layer))  muted.push_back (layer);
        if (preview.isSoloed (layer)) soloed.push_back (layer);
    }
    p.setProperty ("muted", joinInts (muted), nullptr);
    p.setProperty ("soloed", joinInts (soloed), nullptr);
    state.appendChild (p, nullptr);

    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void AudioToMidiProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    // Version 1 was XML.
    if (! state.isValid())
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            state = juce::ValueTree::fromXml (*xml);

    if (! state.hasType ("AudioToMidiState"))
        return;

    lastEditorWidth  = state.getProperty ("editorWidth",  0);
    lastEditorHeight = state.getProperty ("editorHeight", 0);

    if (const auto c = state.getChildWithName ("cleanup"); c.isValid())
    {
        cleanup.quantize = c.getProperty ("quantize", 0.0);
        const int grid = c.getProperty ("grid", (int) atm::Grid::sixteenth);
        cleanup.grid = grid == 1 ? atm::Grid::quarter : grid == 2 ? atm::Grid::eighth : atm::Grid::sixteenth;
    }

    const auto audio = state.getChildWithName ("audio");
    if (! audio.isValid())
    {
        sendChangeMessage();
        return;
    }

    // The audio decodes in the background; everything else is applied once
    // it lands (see finishLoad), so the notes never show without their audio.
    PendingRestore restore;
    restore.generation = ++loadGeneration;

    if (const auto n = state.getChildWithName ("notes"); n.isValid())
    {
        if (auto* block = n.getProperty ("packed").getBinaryData())
            restore.notes = unpackNotes (*block);
        restore.complete = n.getProperty ("complete", true);
    }

    if (const auto t = state.getChildWithName ("tempo"); t.isValid())
    {
        restore.tempo.hasEstimate = t.getProperty ("hasEstimate", false);
        restore.tempo.bpm = t.getProperty ("bpm", 120.0);
        restore.tempo.userEdited = t.getProperty ("userEdited", false);
        restore.tempo.detected.bpm = t.getProperty ("detectedBpm", 120.0);
        restore.tempo.detected.firstBeat = t.getProperty ("firstBeat", 0.0);
        restore.tempo.detected.beatsPerBar = juce::jmax (1, (int) t.getProperty ("beatsPerBar", 4));
        restore.tempo.detected.downbeat = t.getProperty ("downbeat", 0);
        restore.tempo.detected.confidence = (float) (double) t.getProperty ("confidence", 0.0);
    }

    if (const auto p = state.getChildWithName ("preview"); p.isValid())
    {
        restore.mix = (float) (double) p.getProperty ("mix", 0.5);
        restore.muted = splitInts (p.getProperty ("muted", ""));
        restore.soloed = splitInts (p.getProperty ("soloed", ""));
    }

    restore.cleanup = cleanup;
    pendingRestore = std::move (restore);

    if (job != nullptr)
        job->cancel();
    job.reset();

    const juce::String name = audio.getProperty ("name", "audio");
    const juce::String extension = audio.getProperty ("extension", "flac");
    std::shared_ptr<const juce::MemoryBlock> bytes;
    if (auto* block = audio.getProperty ("bytes").getBinaryData())
        bytes = std::make_shared<const juce::MemoryBlock> (*block);

    loadingFileName = name;
    loadError.clear();
    sendChangeMessage();

    const int generation = pendingRestore->generation;
    juce::WeakReference<AudioToMidiProcessor> weakThis (this);

    loaderPool.addJob ([this, weakThis, bytes, extension, name, generation]
    {
        auto decoded = decodeAudioData (bytes, extension, name, [this, generation]
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

//==============================================================================
void AudioToMidiProcessor::loadFile (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const int generation = ++loadGeneration;
    pendingRestore.reset();

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
        stateAudio = std::move (result.stateBytes);
        stateAudioExtension = result.stateExtension;
        preview.setSource (source);

        if (pendingRestore.has_value() && pendingRestore->generation == generation)
            applyRestore (std::move (*pendingRestore));
        else
            startTranscription();
    }
    else
    {
        loadError = result.error;
    }

    pendingRestore.reset();
    sendChangeMessage();
}

void AudioToMidiProcessor::applyRestore (PendingRestore&& restore)
{
    // A session saved mid-transcription is simply run again.
    if (! restore.complete)
    {
        startTranscription();
        return;
    }

    ++jobGeneration;
    transcription = {};
    transcription.model = atm::preferredModel();
    transcription.phase = TranscriptionPhase::done;
    transcription.progress = 1.0f;
    transcription.finalizedThrough = source != nullptr ? source->getLengthSeconds() : 0.0;
    transcription.notes = std::move (restore.notes);

    tempo = restore.tempo;
    tempo.alignedBpm = tempo.bpm;
    cleanup = restore.cleanup;

    preview.setMix (restore.mix);
    for (int layer = 0; layer < atm::Preview::kLayerSlots; ++layer)
    {
        preview.setMuted (layer, false);
        preview.setSoloed (layer, false);
    }
    for (const int layer : restore.muted)  preview.setMuted (layer, true);
    for (const int layer : restore.soloed) preview.setSoloed (layer, true);

    notesChanged();
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

    tempo = {};   // a new run gets a fresh tempo; cleanup settings carry over

    // Mute and solo belong to a transcription's layers: a new run starts with
    // every layer audible. (Otherwise a layer muted on the last file could
    // stay muted on a one-instrument file, where there's no strip to undo it.)
    for (int layer = 0; layer < atm::Preview::kLayerSlots; ++layer)
    {
        preview.setMuted (layer, false);
        preview.setSoloed (layer, false);
    }
    ++notesVersion;

    if (source == nullptr)
    {
        sendChangeMessage();
        return;
    }

    if (! atm::isModelInstalled (transcription.model))
    {
        transcription.phase = TranscriptionPhase::waitingForModel;
        downloader->start (transcription.model);
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
    job->onTempo = [weakThis, generation] (atm::TempoEstimate estimate)
    {
        juce::MessageManager::callAsync ([weakThis, generation, estimate]
        {
            if (auto* self = weakThis.get())
                self->onJobTempo (generation, estimate);
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
    job->onLoaded = [weakThis, generation] (juce::String device)
    {
        juce::MessageManager::callAsync ([weakThis, generation, device]
        {
            if (auto* self = weakThis.get())
                self->onJobLoaded (generation, device);
        });
    };
    job->onFinished = [weakThis, generation] (atm::Outcome outcome, atm::Failure failure, juce::String)
    {
        juce::MessageManager::callAsync ([weakThis, generation, outcome, failure]
        {
            if (auto* self = weakThis.get())
                self->onJobFinished (generation, outcome, failure);
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
    notesChanged();
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

std::optional<atm::ModelSize> AudioToMidiProcessor::smallerModel (atm::ModelSize size)
{
    if (size == atm::kAllModelSizes.front())
        return std::nullopt;

    return (atm::ModelSize) ((int) size - 1);
}

void AudioToMidiProcessor::changeListenerCallback (juce::ChangeBroadcaster* broadcaster)
{
    if (broadcaster == &soundFonts.get())
    {
        if (auto font = soundFonts->get())
            preview.setSoundFont (font);
        return;
    }

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

void AudioToMidiProcessor::onJobTempo (int generation, atm::TempoEstimate estimate)
{
    if (generation != jobGeneration)
        return;

    tempo.detected = estimate;
    tempo.hasEstimate = true;
    if (! tempo.userEdited)
    {
        tempo.bpm = estimate.bpm;
        tempo.alignedBpm = estimate.bpm;
    }

    notesChanged();   // quantize follows the grid
}

void AudioToMidiProcessor::notesChanged()
{
    ++notesVersion;
    preview.setNotes (getCleanNotes());
    sendChangeMessage();
}

void AudioToMidiProcessor::setCleanup (const atm::CleanupSettings& settings)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (settings == cleanup)
        return;

    cleanup = settings;
    notesChanged();
}

const std::vector<atm::MidiNote>& AudioToMidiProcessor::getCleanNotes() const
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (cleanNotesVersion != notesVersion)
    {
        const auto grid = tempo.hasEstimate ? std::optional (tempo.effective()) : std::nullopt;
        cleanNotes = atm::cleanUp (transcription.notes, cleanup, grid);
        cleanNotesVersion = notesVersion;
    }

    return cleanNotes;
}

//==============================================================================
void AudioToMidiProcessor::setBpm (double bpm)
{
    JUCE_ASSERT_MESSAGE_THREAD

    bpm = juce::jlimit (20.0, 400.0, bpm);
    auto& d = tempo.detected;
    const double aligned = tempo.alignedBpm > 0.0 ? tempo.alignedBpm : tempo.bpm;
    const auto near = [] (double a, double b) { return std::abs (a / b - 1.0) < 0.03; };

    // Half the tempo: every other beat remains. If the downbeat was on an odd
    // beat, shift the grid by one old beat so bar 1 is still a beat.
    if (near (bpm, aligned * 0.5))
    {
        if (d.downbeat % 2 != 0)
        {
            d.firstBeat += 60.0 / aligned;
            d.downbeat = (d.downbeat - 1) / 2;
        }
        else
        {
            d.downbeat /= 2;
        }
        tempo.alignedBpm = bpm;
    }
    // Double the tempo: every beat splits in two; the downbeat keeps its place.
    else if (near (bpm, aligned * 2.0))
    {
        d.downbeat = (d.downbeat * 2) % juce::jmax (1, d.beatsPerBar);
        tempo.alignedBpm = bpm;
    }

    tempo.bpm = bpm;
    tempo.userEdited = true;
    notesChanged();
}

void AudioToMidiProcessor::onJobNotes (int generation, std::vector<msl::Note> newNotes, double finalizedThrough)
{
    if (generation != jobGeneration || ! transcription.isRunning())
        return;

    transcription.notes.insert (transcription.notes.end(), newNotes.begin(), newNotes.end());
    transcription.finalizedThrough = juce::jmax (transcription.finalizedThrough, finalizedThrough);
    notesChanged();
}

void AudioToMidiProcessor::onJobLoaded (int generation, juce::String device)
{
    if (generation != jobGeneration)
        return;

    transcription.device = device;
    sendChangeMessage();
}

void AudioToMidiProcessor::onJobFinished (int generation, atm::Outcome outcome, atm::Failure failure)
{
    if (generation != jobGeneration)
        return;

    job.reset();

    // A model file that's damaged or from another release is replaced by
    // itself, once a session, and the run carries on when it lands. One that
    // went missing is simply downloaded again.
    if (outcome == atm::Outcome::failed)
    {
        const auto size = transcription.model;

        if (failure == atm::Failure::modelInvalid && ! replacedModels.contains (size))
        {
            replacedModels.insert (size);
            atm::modelFile (size).deleteFile();
            startTranscription();
            transcription.updatingModel = true;
            sendChangeMessage();
            return;
        }

        if (failure == atm::Failure::modelMissing && ! atm::isModelInstalled (size))
        {
            startTranscription();
            return;
        }
    }

    switch (outcome)
    {
        case atm::Outcome::finished:
            transcription.phase = TranscriptionPhase::done;
            transcription.progress = 1.0f;
            transcription.finalizedThrough = source != nullptr ? source->getLengthSeconds() : 0.0;
            ++notesVersion;
            break;

        case atm::Outcome::cancelled:
            transcription.phase = TranscriptionPhase::cancelled;
            break;

        case atm::Outcome::failed:
            transcription.phase = TranscriptionPhase::failed;
            transcription.failure = failure == atm::Failure::outOfMemory ? failure : atm::Failure::other;
            break;
    }

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioToMidiProcessor();
}
