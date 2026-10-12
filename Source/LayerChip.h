#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    One instrument layer's chip in the piano roll: a 20 px stadium with the
    layer's name. Hover brightens the layer, click selects it, drag it out of
    the window to drop that layer's MIDI on a DAW track, right-click for
    "save as .mid".

    Purely a view of state the canvas owns: the canvas tells it whether it's
    lit (hovered from anywhere, including a note) or selected, and what
    surface shade to fill with so it disappears into the gradient.
*/
class LayerChip : public juce::Component
{
public:
    explicit LayerChip (const juce::String& name);

    void setLit (bool);
    void setSelected (bool);
    void setSurfaceColour (juce::Colour);

    /** Preview state, drawn as the EQ strip's power (mute) and headphones
        (solo) glyphs at the chip's right: engaged ones always show, resting
        ones only while the chip is hovered. */
    void setMuted (bool);
    void setSoloed (bool);

    int getIdealWidth() const;
    static constexpr int kHeight = 20;

    std::function<void (bool isHovering)> onHoverChanged;
    std::function<void()> onClick;
    std::function<void()> onContextMenu;
    std::function<void()> onMuteToggled, onSoloToggled;

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
    enum class Zone { text, mute, solo };
    Zone zoneAt (int x) const noexcept;

    juce::String name;
    bool lit = false, selected = false, isMuted = false, isSoloed = false;
    juce::Colour surface;
    bool dragStarted = false, popupGesture = false;
    Zone pressedZone = Zone::text;
    std::unique_ptr<juce::Drawable> muteIcon, soloIcon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerChip)
};
