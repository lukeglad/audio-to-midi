#pragma once

#include <JuceHeader.h>
#include "SourceAudio.h"

//==============================================================================
/**
    The one surface everything lives on (house rule: everything on the graph).
    So far it draws the lit gradient ground, the source waveform faint
    underneath where the notes will go, the AudioFlower watermark, and the
    drop / loading / error messages. Notes, layers and the hover controls
    join it in later build steps.

    Purely a view: the editor pushes state in, and handles the file drop.
*/
class Canvas : public juce::Component
{
public:
    Canvas();

    void setSource (std::shared_ptr<const SourceAudio>);
    void setLoadStatus (const juce::String& loadingFileName, const juce::String& loadError);
    void setDragHover (bool isHovering);
    void setWordmarkTypeface (juce::Typeface::Ptr);

    void paint (juce::Graphics&) override;

private:
    void paintWaveform (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintWordmark (juce::Graphics&) const;
    void paintMessage (juce::Graphics&) const;

    std::shared_ptr<const SourceAudio> source;
    juce::String loadingFileName, loadError;
    bool dragHover = false;
    juce::Typeface::Ptr wordmarkTypeface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};
