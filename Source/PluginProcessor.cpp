#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioToMidiProcessor::AudioToMidiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

AudioToMidiProcessor::~AudioToMidiProcessor()
{
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
        source = std::move (result.audio);
    else
        loadError = result.error;

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioToMidiProcessor();
}
