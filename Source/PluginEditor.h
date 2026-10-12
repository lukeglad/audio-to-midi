#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"
#include "Canvas.h"
#include "PluginProcessor.h"
#include "TopBar.h"

//==============================================================================
/**
    The editor: a 20 px top bar and the canvas filling everything below it.

    Sizing follows EQ's current architecture. The window is freely resizable;
    every child lives on `content`, which is laid out in a design space of
    (window size / scale) and drawn through one scale transform. The scale is
    kUiScale on comfortable windows and eases to 1.0 on small ones, so text
    stays readable without ballooning, and a bigger window gives a genuinely
    bigger canvas rather than bigger chrome.

    Accepts a file drop anywhere in the window. Composes the status text from
    the processor's transcription state and the shared downloader, refreshed
    on change messages and polled for progress.
*/
class AudioToMidiEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit AudioToMidiEditor (AudioToMidiProcessor&);
    ~AudioToMidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshFromProcessor();
    void showModelMenu();
    juce::File writeLayerMidi (int layerKey, const juce::File& destination) const;
    void saveLayerAs (int layerKey);

    static constexpr float kUiScale = 1.3f;
    static constexpr int   kMinWidth = 540, kMinHeight = 404;
    static constexpr int   kDefaultWidth = 960, kDefaultHeight = 600;

    // Records the window size only while the user is actually dragging the
    // corner. Some hosts (FL in particular) push stale sizes on reopen, and
    // those must never overwrite what the user chose.
    struct SizeMemoryConstrainer : juce::ComponentBoundsConstrainer
    {
        void resizeStart() override { userDragActive = true; }
        void resizeEnd()   override { userDragActive = false; }
        bool userDragActive = false;
    };

    // Carries the scale transform and paints nothing.
    struct ContentComponent : juce::Component
    {
        ContentComponent() { setInterceptsMouseClicks (false, true); }
    };

    AudioToMidiProcessor& processorRef;

    // Declared before every component that uses it, so it outlives them.
    AudioFlowerLookAndFeel lookAndFeel;

    ContentComponent content;
    TopBar topBar;
    Canvas canvas;

    SizeMemoryConstrainer sizeConstrainer;
    std::unique_ptr<juce::FileChooser> saveChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioToMidiEditor)
};
