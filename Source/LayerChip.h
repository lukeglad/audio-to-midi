#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    One instrument layer's chip in the piano roll: a 20 px stadium with the
    layer's name. Hover brightens the layer (and the canvas drops the
    mute / solo strip under it), click selects it, drag it out of the window
    to drop that layer's MIDI on a DAW track, right-click for "save as .mid".

    Purely a view of state the canvas owns: the canvas tells it whether it's
    lit (hovered from anywhere, including a note), selected or muted, and
    what surface shade to fill with so it disappears into the gradient.
*/
class LayerChip : public juce::Component
{
public:
    explicit LayerChip (const juce::String& name);

    /** The "all" chip that drags every audible layer at once: drawn like
        the layer chips (liquid glass, solid ink while hovered), but it
        lights itself on hover rather than being lit by the canvas. */
    void setAllStyle (bool);
    void setLabel (const juce::String&);

    void setLit (bool);
    void setSelected (bool);
    void setSurfaceColour (juce::Colour);

    /** A layer the preview won't play: its name steps down a level. */
    void setMuted (bool);

    int getIdealWidth() const;
    static constexpr int kHeight = 20;

    std::function<void (bool isHovering)> onHoverChanged;
    std::function<void()> onClick;
    std::function<void()> onContextMenu;

    /** Writes the layer's MIDI and returns the file to drag, or an invalid
        file to cancel the drag. Called once per drag gesture. */
    std::function<juce::File()> makeDragFile;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String name;
    bool lit = false, selected = false, isMuted = false, allStyle = false, hovered = false;
    juce::Colour surface;
    bool dragStarted = false, popupGesture = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerChip)
};
