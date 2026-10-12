#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
AudioToMidiEditor::AudioToMidiEditor (AudioToMidiProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    // House face for every bit of UI text (see AudioFlowerLookAndFeel.h).
    AudioFlowerLookAndFeel::setUiTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::IBMPlexMonoRegular_ttf, (size_t) BinaryData::IBMPlexMonoRegular_ttfSize));
    setLookAndFeel (&lookAndFeel);

    canvas.setWordmarkTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::IndieFlowerRegular_ttf, (size_t) BinaryData::IndieFlowerRegular_ttfSize));

    addAndMakeVisible (content);
    content.addAndMakeVisible (canvas);
    content.addAndMakeVisible (topBar);

    processorRef.addChangeListener (this);
    refreshFromProcessor();

    // Constrainer before setResizable, or JUCE installs its own default one.
    sizeConstrainer.setSizeLimits (kMinWidth, kMinHeight, 2400, 1500);
    setConstrainer (&sizeConstrainer);
    setResizable (true, true);

    const int w = processorRef.lastEditorWidth  >= kMinWidth  ? processorRef.lastEditorWidth  : kDefaultWidth;
    const int h = processorRef.lastEditorHeight >= kMinHeight ? processorRef.lastEditorHeight : kDefaultHeight;
    setSize (w, h);

    // Re-apply the remembered size once the host has finished opening the
    // window: FL pushes a stale size straight after construction.
    juce::Component::SafePointer<AudioToMidiEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        const int rw = safeThis->processorRef.lastEditorWidth;
        const int rh = safeThis->processorRef.lastEditorHeight;
        if (rw >= kMinWidth && rh >= kMinHeight && (rw != safeThis->getWidth() || rh != safeThis->getHeight()))
            safeThis->setSize (rw, rh);
    });
}

AudioToMidiEditor::~AudioToMidiEditor()
{
    processorRef.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void AudioToMidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (AudioFlowerLookAndFeel::background());
}

void AudioToMidiEditor::resized()
{
    if (sizeConstrainer.userDragActive && getWidth() >= kMinWidth && getHeight() >= kMinHeight)
    {
        processorRef.lastEditorWidth  = getWidth();
        processorRef.lastEditorHeight = getHeight();
    }

    // Full kUiScale from about 806x624 up; eases to 1.0 by 620x480 so a small
    // window still fits everything instead of clipping it.
    const float scale = juce::jlimit (1.0f, kUiScale,
                                      juce::jmin ((float) getWidth()  / 620.0f,
                                                  (float) getHeight() / 480.0f));
    const int cw = (int) std::ceil ((float) getWidth()  / scale);
    const int ch = (int) std::ceil ((float) getHeight() / scale);

    content.setBounds (0, 0, cw, ch);
    content.setTransform (juce::AffineTransform::scale (scale));

    auto r = juce::Rectangle<int> (0, 0, cw, ch).reduced (8);
    topBar.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    canvas.setBounds (r);
}

//==============================================================================
bool AudioToMidiEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile (juce::File (path)))
            return true;

    return false;
}

void AudioToMidiEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    canvas.setDragHover (true);
}

void AudioToMidiEditor::fileDragExit (const juce::StringArray&)
{
    canvas.setDragHover (false);
}

void AudioToMidiEditor::filesDropped (const juce::StringArray& files, int, int)
{
    canvas.setDragHover (false);

    // One file in, by design. With several dropped, the first usable one wins.
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (isSupportedAudioFile (file))
        {
            processorRef.loadFile (file);
            return;
        }
    }
}

//==============================================================================
void AudioToMidiEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromProcessor();
}

void AudioToMidiEditor::refreshFromProcessor()
{
    const auto source = processorRef.getSource();

    canvas.setSource (source);
    canvas.setLoadStatus (processorRef.getLoadingFileName(), processorRef.getLoadError());

    if (source != nullptr)
        topBar.setFile (source->fileName, source->getLengthSeconds());
    else
        topBar.setFile ({}, 0.0);
}
