#pragma once

#include <JuceHeader.h>
#include "SourceAudio.h"

//==============================================================================
/**
    The instrument plugin. For now it owns the dropped audio and decodes it
    in the background; transcription, the preview synth and state recall of
    the audio come in later build steps.

    Load state lives on the message thread only. Listeners (the editor) get a
    change message whenever it moves.
*/
class AudioToMidiProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster
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
    // Source audio. Message thread only.

    /** Starts decoding `file` in the background. A newer call supersedes an
        older decode that hasn't finished. */
    void loadFile (const juce::File& file);

    /** The most recently loaded file, or null. Survives a failed or
        in-progress load of a newer one, so a bad drop never loses work. */
    std::shared_ptr<const SourceAudio> getSource() const noexcept { return source; }

    /** Name of the file being decoded, or empty when idle. */
    const juce::String& getLoadingFileName() const noexcept { return loadingFileName; }

    /** Why the last load failed, or empty. Cleared by the next load. */
    const juce::String& getLoadError() const noexcept { return loadError; }

    //==========================================================================
    // Editor size, saved with the session but recorded only on a real corner
    // drag (see the editor's SizeMemoryConstrainer). 0 = never resized.
    int lastEditorWidth = 0, lastEditorHeight = 0;

private:
    void finishLoad (int generation, DecodeResult result);

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;

    // Bumped by every loadFile(). A decode whose number is no longer current
    // stops early and its result is dropped.
    std::atomic<int> loadGeneration { 0 };

    // One thread, so decodes run one at a time. Declared last: it is
    // destroyed first, waiting out any running decode before the members
    // that decode reads disappear.
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions{}.withThreadName ("audio file decode")
                                                          .withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioToMidiProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioToMidiProcessor)
};
