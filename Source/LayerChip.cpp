#include "LayerChip.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 12.0f;
    constexpr int   kPaddingX = 11;
    constexpr int   kDragThreshold = 4;
}

LayerChip::LayerChip (const juce::String& n)
    : name (n), surface (LnF::background())
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LayerChip::setLit (bool shouldBeLit)
{
    if (shouldBeLit != lit) { lit = shouldBeLit; repaint(); }
}

void LayerChip::setSelected (bool shouldBeSelected)
{
    if (shouldBeSelected != selected) { selected = shouldBeSelected; repaint(); }
}

void LayerChip::setMuted (bool shouldBeMuted)
{
    if (shouldBeMuted != isMuted) { isMuted = shouldBeMuted; repaint(); }
}

void LayerChip::setSurfaceColour (juce::Colour c)
{
    if (c != surface) { surface = c; repaint(); }
}

int LayerChip::getIdealWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), name)) + kPaddingX * 2;
}

void LayerChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;

    // Lit: the house hot-chip treatment (ink fill, ground text). Otherwise a
    // surface-shaded stadium, so notes passing under it don't show through,
    // with copper hairline + text when selected (copper = the selected layer).
    g.setColour (lit ? LnF::ink() : surface);
    g.fillRoundedRectangle (r, radius);

    if (! lit)
    {
        g.setColour (selected ? LnF::accent() : LnF::ink().withAlpha (0.45f));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    // A silenced layer's name steps down to "engaged-suppressed".
    g.setColour (lit ? LnF::background() : selected ? LnF::accent() : LnF::ink().withAlpha (isMuted ? 0.45f : 0.85f));
    g.setFont (LnF::uiFont (kTextSize));
    g.drawText (name, getLocalBounds(), juce::Justification::centred, false);
}

void LayerChip::mouseEnter (const juce::MouseEvent&)
{
    if (onHoverChanged) onHoverChanged (true);
}

void LayerChip::mouseExit (const juce::MouseEvent&)
{
    if (onHoverChanged) onHoverChanged (false);
}

void LayerChip::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    popupGesture = e.mods.isPopupMenu();

    if (popupGesture && onContextMenu)
        onContextMenu();
}

void LayerChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || popupGesture || e.getDistanceFromDragStart() < kDragThreshold)
        return;

    dragStarted = true;

    if (makeDragFile == nullptr)
        return;

    const auto file = makeDragFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void LayerChip::mouseUp (const juce::MouseEvent&)
{
    if (! dragStarted && ! popupGesture && onClick)
        onClick();
}
