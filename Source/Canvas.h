#pragma once

#include <JuceHeader.h>
#include "Chip.h"
#include "SourceAudio.h"

//==============================================================================
/**
    The one surface everything lives on (house rule: everything on the graph).
    So far it draws the lit gradient ground, the source waveform faint
    underneath where the notes will go, the transcription's progress line,
    the AudioFlower watermark, and one status message with an optional
    action chip under it. Notes, layers and the hover controls join it in
    later build steps.

    Purely a view: the editor pushes state in and handles the file drop.
*/
class Canvas : public juce::Component
{
public:
    /** What to say in the middle of the canvas while nothing else claims it
        (a load in progress, a drag-over or a load error take precedence). */
    struct Status
    {
        juce::String primary, secondary;
        float primaryAlpha = 0.75f;
        juce::String action;   // chip text; empty = no chip
    };

    Canvas();

    void setSource (std::shared_ptr<const SourceAudio>);
    void setLoadStatus (const juce::String& loadingFileName, const juce::String& loadError);
    void setDragHover (bool isHovering);
    void setWordmarkTypeface (juce::Typeface::Ptr);
    void setStatus (const Status&);

    /** Where the transcription has got to, in seconds; negative hides the line. */
    void setProgressTime (double seconds);

    std::function<void()> onAction;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void paintWaveform (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintWordmark (juce::Graphics&) const;
    void paintMessage (juce::Graphics&) const;
    juce::Rectangle<float> waveformArea() const;
    void updateChip();

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;
    bool dragHover = false;
    juce::Typeface::Ptr wordmarkTypeface;
    Status status;
    double progressTime = -1.0;
    Chip actionChip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
