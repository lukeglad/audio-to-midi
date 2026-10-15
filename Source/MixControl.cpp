#include "MixControl.h"
#include "AudioFlowerLookAndFeel.h"
#include "Glass.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    // The grid pill's type and spacing, so the two pills read as a pair.
    constexpr float kTextSize = 12.0f;
    constexpr int   kPadX = 10;
    constexpr int   kGap = 10;
    constexpr int   kTrackW = 72;
    constexpr float kThumb = 10.0f;

    int widthOf (const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), text));
    }
}

MixPill::MixPill()
    : surface (LnF::background())
{
}

int MixPill::getIdealWidth() const
{
    return kPadX + widthOf ("audio") + kGap + kTrackW + kGap + widthOf ("midi") + kPadX;
}

juce::Rectangle<float> MixPill::audioBounds() const
{
    return { (float) kPadX, 0.0f, (float) widthOf ("audio"), (float) getHeight() };
}

juce::Rectangle<float> MixPill::trackBounds() const
{
    // The whole height is the hit area; the line is drawn in its middle.
    return { (float) (kPadX + widthOf ("audio") + kGap), 0.0f, (float) kTrackW, (float) getHeight() };
}

juce::Rectangle<float> MixPill::midiBounds() const
{
    return { trackBounds().getRight() + (float) kGap, 0.0f, (float) widthOf ("midi"), (float) getHeight() };
}

MixPill::Part MixPill::partAt (juce::Point<float> p) const
{
    // Generous: the gaps either side of the track belong to the track.
    if (trackBounds().expanded ((float) kGap * 0.5f, 0.0f).contains (p)) return Part::track;
    if (audioBounds().expanded ((float) kGap * 0.5f, 0.0f).contains (p)) return Part::audio;
    if (midiBounds().expanded ((float) kGap * 0.5f, 0.0f).contains (p))  return Part::midi;
    return Part::none;
}

void MixPill::setValue (float mix01)
{
    mix01 = juce::jlimit (0.0f, 1.0f, mix01);
    if (! juce::exactlyEqual (mix01, value) && ! dragging)
    {
        value = mix01;
        repaint();
    }
}

void MixPill::set (float mix01)
{
    mix01 = juce::jlimit (0.0f, 1.0f, mix01);
    if (juce::exactlyEqual (mix01, value))
        return;

    value = mix01;
    repaint();
    if (onChanged)
        onChanged (value);
}

void MixPill::setSurfaceColour (juce::Colour c)
{
    if (c != surface) { surface = c; repaint(); }
}

void MixPill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (! Glass::paint (g, *this, r, surface))
    {
        g.setColour (surface.withAlpha (0.88f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    // The labels brighten toward whichever end the dot is near: 45% at the
    // far end, 90% at the near one, a hovered label full.
    g.setFont (LnF::uiFont (kTextSize));
    const float audioAlpha = 0.45f + 0.45f * (1.0f - value), midiAlpha = 0.45f + 0.45f * value;
    g.setColour (LnF::ink().withAlpha (hovered == Part::audio ? 1.0f : audioAlpha));
    g.drawText ("audio", audioBounds(), juce::Justification::centredLeft, false);
    g.setColour (LnF::ink().withAlpha (hovered == Part::midi ? 1.0f : midiAlpha));
    g.drawText ("midi", midiBounds(), juce::Justification::centredLeft, false);

    // The track: a soft line, nothing more (the middle tick went, 2026-10-15).
    const auto t = trackBounds();
    const float cy = t.getCentreY();
    g.setColour (LnF::ink().withAlpha (0.22f));
    g.fillRoundedRectangle (t.getX(), cy - 2.0f, t.getWidth(), 4.0f, 2.0f);

    // The dot, ringed in the ground so it sits clear of the line.
    const float x = t.getX() + kThumb * 0.5f + (t.getWidth() - kThumb) * value;
    const auto dot = juce::Rectangle<float> (kThumb, kThumb).withCentre ({ x, cy });
    g.setColour (LnF::background().withAlpha (0.7f));
    g.fillEllipse (dot.expanded (2.0f));
    g.setColour (LnF::ink().withAlpha (dragging || hovered == Part::track ? 1.0f : 0.85f));
    g.fillEllipse (dot);
}

void MixPill::mouseMove (const juce::MouseEvent& e)
{
    const auto part = partAt (e.position);
    if (part != hovered)
    {
        hovered = part;
        setMouseCursor (part == Part::track ? juce::MouseCursor::LeftRightResizeCursor
                        : part == Part::none ? juce::MouseCursor::NormalCursor
                                             : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void MixPill::mouseExit (const juce::MouseEvent&)
{
    if (hovered != Part::none) { hovered = Part::none; repaint(); }
}

void MixPill::mouseDown (const juce::MouseEvent& e)
{
    if (partAt (e.position) != Part::track)
        return;

    dragging = true;
    const auto t = trackBounds();
    set ((e.position.x - t.getX() - kThumb * 0.5f) / (t.getWidth() - kThumb));
}

void MixPill::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto t = trackBounds();
    set ((e.position.x - t.getX() - kThumb * 0.5f) / (t.getWidth() - kThumb));
}

void MixPill::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        repaint();
        return;
    }

    // A label takes the mix all the way to its end.
    if (e.mouseWasClicked())
    {
        const auto part = partAt (e.position);
        if (part == Part::audio) set (0.0f);
        else if (part == Part::midi) set (1.0f);
    }
}

void MixPill::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (partAt (e.position) == Part::track)
        set (kDefault);
}
