#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"
#include "Glass.h"

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

    /** Glyph size, in its pill. */
    static constexpr float kGlyphSize = 17.6f;   // play ~12 px tall: a triangle reads smaller than the 10 px record dot

    /** The shade its glass pill is tinted with (surfaceAt its height). */
    void setSurfaceColour (juce::Colour c) { if (c != surface) { surface = c; repaint(); } }

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
        // 12.25 units, centred (~9 px): a square reads larger than the
        // record dot it sits above, so it stays a touch narrower than 10.
        p.addRoundedRectangle (5.875f, 5.875f, 12.25f, 12.25f, 1.0f);
        return p;
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        // Its own glass pill (user's pick 2026-10-15, "two pills" in
        // mockups/play-record-bubbles.html), with a flat fallback.
        const auto pill = getLocalBounds().toFloat();
        if (! Glass::paint (g, *this, pill, surface))
        {
            g.setColour (surface);
            g.fillRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f);
            g.setColour (AudioFlowerLookAndFeel::ink().withAlpha (0.40f));
            g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
        }

        const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (kGlyphSize, kGlyphSize);
        const float scale = kGlyphSize / 24.0f;
        auto shape = playing ? stopShape() : playShape();
        shape.applyTransform (juce::AffineTransform::scale (scale).translated (box.getX(), box.getY()));

        // Dimmed, not hidden, with nothing to play: the pill keeps its place.
        const float alpha = ! isEnabled() ? 0.25f : (isMouseOver || isButtonDown) ? 1.0f : 0.75f;

        // Drawn opaque inside a layer that is then faded as a whole: the
        // stroke that rounds the triangle's corners overlaps its fill, and
        // two translucent coats made a brighter rim, so the play read as
        // outlined next to the record dot's single fill.
        g.beginTransparencyLayer (alpha);
        g.setColour (AudioFlowerLookAndFeel::ink());
        g.fillPath (shape);
        if (! playing)
            g.strokePath (shape, juce::PathStrokeType (1.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.endTransparencyLayer();
    }

    bool playing = false;
    juce::Colour surface { AudioFlowerLookAndFeel::background() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayButton)
};
