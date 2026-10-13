#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"

//==============================================================================
/**
    The preview's play / stop, in the corner of the waveform band above the
    piano keys, next to the audio it plays.

    Drawn here rather than taken from Tabler, and FILLED: a deliberate,
    recorded exception to the house's outline-only icon rule (user decision
    2026-10-13, option "E full" in mockups/play-glyphs.html). A soft-cornered
    triangle and a rounded square, solid ink at the resting 75%, full on
    hover. Every other glyph in the plugin stays outline.
*/
class PlayButton : public juce::Button
{
public:
    PlayButton() : juce::Button ("play")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setPlaying (bool shouldShowStop)
    {
        if (shouldShowStop != playing) { playing = shouldShowStop; repaint(); }
    }

    /** Glyph size; the button's bounds can be larger, as a hit area. */
    static constexpr float kGlyphSize = 26.0f;

private:
    // Both shapes in a 24x24 box, as in the mockup.
    static juce::Path playShape()
    {
        juce::Path p;
        p.startNewSubPath (8.2f, 5.3f);
        p.quadraticTo (7.0f, 4.6f, 7.0f, 6.0f);
        p.lineTo (7.0f, 18.0f);
        p.quadraticTo (7.0f, 19.4f, 8.2f, 18.7f);
        p.lineTo (18.2f, 12.8f);
        p.quadraticTo (19.4f, 12.0f, 18.2f, 11.2f);
        p.closeSubPath();
        return p;
    }

    // Barely softened: with more rounding (3 units, as first built) it read
    // as a blob rather than a stop square at this size.
    static juce::Path stopShape()
    {
        juce::Path p;
        // 13.5 units, centred: optically the same size as the play
        // triangle, which spans ~16 units tall with its rounding stroke.
        p.addRoundedRectangle (5.25f, 5.25f, 13.5f, 13.5f, 0.9f);
        return p;
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (kGlyphSize, kGlyphSize);
        const float scale = kGlyphSize / 24.0f;
        auto shape = playing ? stopShape() : playShape();
        shape.applyTransform (juce::AffineTransform::scale (scale).translated (box.getX(), box.getY()));

        g.setColour (AudioFlowerLookAndFeel::ink().withAlpha ((isMouseOver || isButtonDown) ? 1.0f : 0.75f));
        g.fillPath (shape);

        // A matching stroke rounds the play triangle's corners the way the
        // mockup's did; the stop square keeps its edges crisp.
        if (! playing)
            g.strokePath (shape, juce::PathStrokeType (1.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayButton)
};
