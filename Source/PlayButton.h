#pragma once

#include <JuceHeader.h>
#include "AudioFlowerIcons.h"

//==============================================================================
/**
    The preview's play / stop, as the house outline glyphs (Tabler
    player-play / player-stop). Resting at ink 0.75, full on hover. Lives in
    the corner of the waveform band, above the piano keys, next to the audio
    it plays.
*/
class PlayButton : public juce::Button
{
public:
    PlayButton()
        : juce::Button ("play"),
          playIcon (AudioFlowerIcons::makeTablerIcon (AudioFlowerIcons::kPlay)),
          stopIcon (AudioFlowerIcons::makeTablerIcon (AudioFlowerIcons::kStop))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setPlaying (bool shouldShowStop)
    {
        if (shouldShowStop != playing) { playing = shouldShowStop; repaint(); }
    }

    /** Glyph size; the button's bounds can be larger, as a hit area. */
    static constexpr float kGlyphSize = 20.0f;

private:
    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto* icon = playing ? stopIcon.get() : playIcon.get();
        if (icon == nullptr)
            return;

        icon->drawWithin (g, getLocalBounds().toFloat().withSizeKeepingCentre (kGlyphSize, kGlyphSize),
                          juce::RectanglePlacement::centred, (isMouseOver || isButtonDown) ? 1.0f : 0.75f);
    }

    bool playing = false;
    std::unique_ptr<juce::Drawable> playIcon, stopIcon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayButton)
};
