#include "LayerChip.h"
#include "AudioFlowerIcons.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 12.0f;
    constexpr int   kPaddingX = 11;
    constexpr int   kDragThreshold = 4;
    constexpr int   kIconSize = 12, kIconGap = 6;
    constexpr int   kIconZone = kIconSize * 2 + kIconGap * 2;   // both glyphs, right of the text
}

LayerChip::LayerChip (const juce::String& n)
    : name (n), surface (LnF::background()),
      muteIcon (AudioFlowerIcons::makeTablerIcon (AudioFlowerIcons::kPower)),
      soloIcon (AudioFlowerIcons::makeTablerIcon (AudioFlowerIcons::kHeadphones))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LayerChip::setMuted (bool shouldBeMuted)
{
    if (shouldBeMuted != isMuted) { isMuted = shouldBeMuted; repaint(); }
}

void LayerChip::setSoloed (bool shouldBeSoloed)
{
    if (shouldBeSoloed != isSoloed) { isSoloed = shouldBeSoloed; repaint(); }
}

LayerChip::Zone LayerChip::zoneAt (int x) const noexcept
{
    const int zoneStart = getWidth() - kPaddingX - kIconZone + kIconGap;
    if (x < zoneStart)
        return Zone::text;
    return x < zoneStart + kIconSize + kIconGap ? Zone::mute : Zone::solo;
}

void LayerChip::setLit (bool shouldBeLit)
{
    if (shouldBeLit != lit) { lit = shouldBeLit; repaint(); }
}

void LayerChip::setSelected (bool shouldBeSelected)
{
    if (shouldBeSelected != selected) { selected = shouldBeSelected; repaint(); }
}

void LayerChip::setSurfaceColour (juce::Colour c)
{
    if (c != surface) { surface = c; repaint(); }
}

int LayerChip::getIdealWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), name)) + kPaddingX * 2 + kIconZone;
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

    // Text: a muted layer's name steps down to "engaged-suppressed".
    const auto textColour = lit ? LnF::background() : selected ? LnF::accent() : LnF::ink().withAlpha (isMuted ? 0.45f : 0.85f);
    g.setColour (textColour);
    g.setFont (LnF::uiFont (kTextSize));
    g.drawText (name, getLocalBounds().withTrimmedRight (kIconZone).withTrimmedLeft (kPaddingX), juce::Justification::centredLeft, false);

    // Glyphs: engaged ones at 0.45 (the EQ strip's muted power / listening
    // headphones alpha), resting ones at 0.75 while hovered, else hidden.
    const bool hovered = isMouseOverOrDragging();
    const auto iconColour = lit ? LnF::background() : LnF::ink();
    const int zoneStart = getWidth() - kPaddingX - kIconZone + kIconGap;
    const auto drawGlyph = [&] (juce::Drawable* icon, int x, bool engaged)
    {
        const float alpha = engaged ? (lit ? 0.8f : 0.45f) : hovered ? 0.75f : 0.0f;
        if (icon == nullptr || alpha <= 0.0f)
            return;
        icon->replaceColour (juce::Colours::black, iconColour);
        icon->replaceColour (LnF::ink(), iconColour);
        icon->replaceColour (LnF::background(), iconColour);
        icon->drawWithin (g, juce::Rectangle<float> ((float) x, (float) (getHeight() - kIconSize) * 0.5f, (float) kIconSize, (float) kIconSize),
                          juce::RectanglePlacement::centred, alpha);
    };
    drawGlyph (muteIcon.get(), zoneStart, isMuted);
    drawGlyph (soloIcon.get(), zoneStart + kIconSize + kIconGap, isSoloed);
}

void LayerChip::mouseEnter (const juce::MouseEvent&)
{
    repaint();   // the resting glyphs appear
    if (onHoverChanged) onHoverChanged (true);
}

void LayerChip::mouseExit (const juce::MouseEvent&)
{
    repaint();
    if (onHoverChanged) onHoverChanged (false);
}

void LayerChip::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    popupGesture = e.mods.isPopupMenu();
    pressedZone = zoneAt (e.x);

    if (popupGesture && onContextMenu)
        onContextMenu();
}

void LayerChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || popupGesture || pressedZone != Zone::text || e.getDistanceFromDragStart() < kDragThreshold)
        return;

    dragStarted = true;

    if (makeDragFile == nullptr)
        return;

    const auto file = makeDragFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void LayerChip::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted || popupGesture)
        return;

    switch (zoneAt (e.x))
    {
        case Zone::mute: if (onMuteToggled) onMuteToggled(); break;
        case Zone::solo: if (onSoloToggled) onSoloToggled(); break;
        case Zone::text: if (onClick) onClick(); break;
    }
}
