#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"
#include "BinaryData.h"
#include "Canvas.h"
#include "GlassMenu.h"
#include "GuideOverlay.h"
#include "PluginProcessor.h"
#include "Engine/MidiExport.h"
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
    atm::ExportTiming exportTiming() const;
    juce::File writeLayersMidi (const std::vector<int>& layerKeys, const juce::File& destination) const;
    void saveLayersAs (const std::vector<int>& layerKeys, bool allOfThem);

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

    // The house typeface, injected into the LookAndFeel while any editor
    // exists and released when the last one goes. Held through a
    // SharedResourcePointer rather than left in the LookAndFeel's static:
    // a Typeface that survives to process exit (or plugin unload) is torn
    // down after CoreText is, and that aborts the host.
    struct HouseFonts
    {
        HouseFonts()
        {
            AudioFlowerLookAndFeel::setUiTypeface (juce::Typeface::createSystemTypefaceFor (
                BinaryData::IBMPlexMonoRegular_ttf, (size_t) BinaryData::IBMPlexMonoRegular_ttfSize));
        }
        ~HouseFonts() { AudioFlowerLookAndFeel::setUiTypeface (nullptr); }
    };
    juce::SharedResourcePointer<HouseFonts> houseFonts;

    // Declared before every component that uses it, so it outlives them.
    AudioFlowerLookAndFeel lookAndFeel;

    ContentComponent content;
    TopBar topBar;
    Canvas canvas;
    GuideOverlay guide;
    GlassMenu menu;   // last: above everything, the guide included

    SizeMemoryConstrainer sizeConstrainer;

    /**
        Turns the engine's progress, which only moves once per 5 s chunk,
        into a steady climb. Between reports it advances at the measured pace
        of a chunk, but never past 95% of the way to the next chunk boundary:
        it holds there until the engine confirms, so it can pause but never
        overshoot and jump back. The first chunk uses a typical pace for the
        model size until a real one is measured.
    */
    struct ProgressSmoother
    {
        float update (double nowMs, float reported, double lengthSeconds, atm::ModelSize);

        int run = -1;
        double lastReportMs = -1.0, msPerChunk = 0.0;
        bool measured = false;
        float base = 0.0f, shown = 0.0f;
    };
    ProgressSmoother progressSmoother;
    std::unique_ptr<juce::FileChooser> saveChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioToMidiEditor)
};
