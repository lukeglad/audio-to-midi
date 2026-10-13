#pragma once

#include <JuceHeader.h>
#include "Engine/Cleanup.h"
#include "Engine/ModelDownloader.h"
#include "Engine/Preview.h"
#include "Engine/SoundFont.h"
#include "Engine/TranscriptionService.h"
#include "SourceAudio.h"

#include <set>

//==============================================================================
enum class TranscriptionPhase
{
    none,              // nothing loaded, or a load that produced nothing
    waitingForModel,   // audio loaded, chosen model not installed (downloading, failed, or cancelled)
    queued,
    resampling,
    loadingModel,
    transcribing,
    done,
    cancelled,
    failed
};

/** Message thread only. Replaced wholesale when a new run starts. */
struct TranscriptionState
{
    TranscriptionPhase phase = TranscriptionPhase::none;
    float progress = -1.0f;            // within loadingModel / transcribing; negative = nothing to measure yet
    double finalizedThrough = 0.0;     // seconds of audio the notes below are complete for
    std::vector<msl::Note> notes;      // raw engine output, in arrival order
    atm::Failure failure = atm::Failure::none;   // failed only
    juce::String device;               // "metal" / "cpu", once the model loaded
    atm::ModelSize model = atm::kDefaultModelSize;
    bool updatingModel = false;        // waiting on a download that replaces a damaged or outdated model

    bool isRunning() const noexcept
    {
        return phase == TranscriptionPhase::queued || phase == TranscriptionPhase::resampling
            || phase == TranscriptionPhase::loadingModel || phase == TranscriptionPhase::transcribing;
    }
};

/** The tempo the export grid uses: detected by the worker, editable here. */
struct TempoState
{
    bool hasEstimate = false;
    atm::TempoEstimate detected;
    double bpm = 120.0;          // what the user sees; follows `detected` until edited
    bool userEdited = false;
    double alignedBpm = 0.0;     // the tempo the downbeat was last placed for (see setBpm)

    /** The detected grid with the user's tempo applied. */
    atm::TempoEstimate effective() const noexcept
    {
        auto t = detected;
        t.bpm = bpm;
        return t;
    }
};

//==============================================================================
/**
    The instrument plugin. Owns the dropped audio and its transcription;
    transcribes automatically whenever audio lands and a model is installed.
    The preview synth and state recall of audio and notes come in later steps.

    All state here is message-thread only. Listeners (the editor) get a change
    message whenever something moves; the editor also polls for progress.
*/
class AudioToMidiProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::ChangeListener
{
public:
    AudioToMidiProcessor();
    ~AudioToMidiProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                             { return 1; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const juce::String getProgramName (int) override          { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Source audio.

    /** Starts decoding `file` in the background. A newer call supersedes an
        older decode that hasn't finished. Transcription follows automatically. */
    void loadFile (const juce::File& file);

    /** What getStateInformation stores so the audio comes back without the
        file. Null when the loaded file couldn't be encoded. */
    std::shared_ptr<const juce::MemoryBlock> getStateAudio() const noexcept { return stateAudio; }

    /** The most recently loaded file, or null. Survives a failed or
        in-progress load of a newer one, so a bad drop never loses work. */
    std::shared_ptr<const SourceAudio> getSource() const noexcept { return source; }

    const juce::String& getLoadingFileName() const noexcept { return loadingFileName; }
    const juce::String& getLoadError() const noexcept { return loadError; }

    //==========================================================================
    // Transcription and models.

    const TranscriptionState& getTranscription() const noexcept { return transcription; }

    /** Changes whenever a new transcription replaces the current one, so a
        view knows to drop what it built from the old notes. */
    int getTranscriptionRun() const noexcept { return jobGeneration; }

    /** The host's tempo, read from the playhead on the audio thread; 120 in
        the standalone or before the host reports one. Any thread. */
    double getHostBpm() const noexcept { return hostBpm.load(); }

    /** Runs the loaded audio through the preferred model, replacing any
        earlier result. With the model not installed, starts its download
        (dropping a file is the consent) and transcribes once it lands. */
    void startTranscription();
    void cancelTranscription();

    /** Makes `size` the preferred model. Installed: re-transcribes the loaded
        audio with it. Not installed: starts its download, and transcribes once
        it lands. */
    void chooseModel (atm::ModelSize size);
    void cancelDownload();

    /** After a failure or cancellation: run again, or restart the download. */
    void retry();

    /** The next model down from `size`, for when `size` doesn't fit in memory. */
    static std::optional<atm::ModelSize> smallerModel (atm::ModelSize size);

    atm::ModelDownloader& getDownloader() noexcept { return *downloader; }

    //==========================================================================
    // Tempo. Message thread only.

    const TempoState& getTempo() const noexcept { return tempo; }
    /** The user's tempo. Landing on about half or double the tempo the grid
        was last aligned for re-places the downbeat so bar 1 stays on a real
        beat (what the removed ½ / ×2 buttons did). */
    void setBpm (double bpm);

    //==========================================================================
    // Cleanup. Message thread only.

    /** Cleanup settings are the user's working preferences: they carry over
        from one file to the next (the tempo doesn't). */
    const atm::CleanupSettings& getCleanup() const noexcept { return cleanup; }
    void setCleanup (const atm::CleanupSettings&);

    /** The notes as shown and exported: raw notes after cleanup. Recomputed
        only when something they depend on changed. */
    const std::vector<atm::MidiNote>& getCleanNotes() const;

    /** Changes whenever getCleanNotes() would change. */
    int getNotesVersion() const noexcept { return notesVersion; }

    //==========================================================================
    // Preview playback: the original audio and a synth rendering of the
    // notes, with its own transport. Message-thread calls; it renders itself
    // in processBlock.
    atm::Preview& getPreview() noexcept { return preview; }

    //==========================================================================
    // Editor size, saved with the session but recorded only on a real corner
    // drag (see the editor's SizeMemoryConstrainer). 0 = never resized.
    int lastEditorWidth = 0, lastEditorHeight = 0;

private:
    void finishLoad (int generation, DecodeResult result);

    // A saved session being restored: decoding its audio takes a moment in
    // the background, and everything else waits for it so a half-restored
    // plugin is never shown.
    struct PendingRestore
    {
        int generation = 0;
        std::vector<msl::Note> notes;
        bool complete = true;
        TempoState tempo;
        atm::CleanupSettings cleanup;
        float mix = 0.5f;
        std::vector<int> muted, soloed;
    };
    std::optional<PendingRestore> pendingRestore;
    void applyRestore (PendingRestore&&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void onJobProgress (int generation, atm::Phase, float progress);
    void onJobTempo (int generation, atm::TempoEstimate);
    void notesChanged();
    void onJobNotes (int generation, std::vector<msl::Note> newNotes, double finalizedThrough);
    void onJobLoaded (int generation, juce::String device);
    void onJobFinished (int generation, atm::Outcome, atm::Failure);

    std::shared_ptr<const SourceAudio> source;
    std::shared_ptr<const juce::MemoryBlock> stateAudio;
    juce::String stateAudioExtension;
    juce::String loadingFileName, loadError;
    std::atomic<int> loadGeneration { 0 };

    TranscriptionState transcription;
    TempoState tempo;
    atm::Preview preview;
    atm::CleanupSettings cleanup;
    int notesVersion = 0;
    mutable std::vector<atm::MidiNote> cleanNotes;
    mutable int cleanNotesVersion = -1;
    std::atomic<double> hostBpm { 120.0 };
    std::shared_ptr<atm::TranscriptionJob> job;
    int jobGeneration = 0;   // a callback from an older job is ignored
    std::set<atm::ModelSize> replacedModels;   // replaced once this session; a second failure is reported

    // Shared by every instance in the process. Declared before the pool and
    // the job so they go last: the service's destructor waits for the running
    // job to notice its cancel.
    juce::SharedResourcePointer<atm::TranscriptionService> service;
    juce::SharedResourcePointer<atm::ModelDownloader> downloader;
    juce::SharedResourcePointer<atm::SoundFontLibrary> soundFonts;

    // One thread, so decodes run one at a time. Declared last: it is
    // destroyed first, waiting out any running decode before the members
    // that decode reads disappear.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("audio file decode")
                                                          .withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioToMidiProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioToMidiProcessor)
};
