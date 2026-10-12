#pragma once

#include <JuceHeader.h>
#include "Engine/ModelDownloader.h"
#include "Engine/TranscriptionService.h"
#include "SourceAudio.h"

//==============================================================================
enum class TranscriptionPhase
{
    none,              // nothing loaded, or a load that produced nothing
    waitingForModel,   // audio loaded, chosen model not installed (downloading, failed, or not started)
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
    juce::String error;                // failed only
    juce::String device;               // "metal" / "cpu", once the model loaded
    atm::ModelSize model = atm::kDefaultModelSize;

    bool isRunning() const noexcept
    {
        return phase == TranscriptionPhase::queued || phase == TranscriptionPhase::resampling
            || phase == TranscriptionPhase::loadingModel || phase == TranscriptionPhase::transcribing;
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

    /** The most recently loaded file, or null. Survives a failed or
        in-progress load of a newer one, so a bad drop never loses work. */
    std::shared_ptr<const SourceAudio> getSource() const noexcept { return source; }

    const juce::String& getLoadingFileName() const noexcept { return loadingFileName; }
    const juce::String& getLoadError() const noexcept { return loadError; }

    //==========================================================================
    // Transcription and models.

    const TranscriptionState& getTranscription() const noexcept { return transcription; }

    /** Runs the loaded audio through the preferred model, replacing any
        earlier result. With no model installed, waits for one. */
    void startTranscription();
    void cancelTranscription();

    /** Makes `size` the preferred model. Installed: re-transcribes the loaded
        audio with it. Not installed: starts its download, and transcribes once
        it lands. */
    void chooseModel (atm::ModelSize size);
    void cancelDownload();

    /** After a failure or cancellation: run again, or restart the download. */
    void retry();

    atm::ModelDownloader& getDownloader() noexcept { return *downloader; }

    //==========================================================================
    // Editor size, saved with the session but recorded only on a real corner
    // drag (see the editor's SizeMemoryConstrainer). 0 = never resized.
    int lastEditorWidth = 0, lastEditorHeight = 0;

private:
    void finishLoad (int generation, DecodeResult result);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void onJobProgress (int generation, atm::Phase, float progress);
    void onJobNotes (int generation, std::vector<msl::Note> newNotes, double finalizedThrough);
    void onJobFinished (int generation, atm::Outcome, juce::String error, juce::String device);

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;
    std::atomic<int> loadGeneration { 0 };

    TranscriptionState transcription;
    std::shared_ptr<atm::TranscriptionJob> job;
    int jobGeneration = 0;   // a callback from an older job is ignored

    // Shared by every instance in the process. Declared before the pool and
    // the job so they go last: the service's destructor waits for the running
    // job to notice its cancel.
    juce::SharedResourcePointer<atm::TranscriptionService> service;
    juce::SharedResourcePointer<atm::ModelDownloader> downloader;

    // One thread, so decodes run one at a time. Declared last: it is
    // destroyed first, waiting out any running decode before the members
    // that decode reads disappear.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("audio file decode")
                                                          .withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioToMidiProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioToMidiProcessor)
};
