#include "TopBar.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    juce::String formatLength (double seconds)
    {
        const int total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

TopBar::TopBar()
{
    setInterceptsMouseClicks (false, false);
}

void TopBar::setFile (const juce::String& name, double seconds)
{
    if (name == fileName && juce::exactlyEqual (seconds, lengthSeconds))
        return;

    fileName = name;
    lengthSeconds = seconds;
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    if (fileName.isEmpty())
        return;

    // Name at full ink, length dimmed after it, the pair centred on the bar
    // the way EQ centres its preset name. Measured in the same 14 px face,
    // so a long name is cut with an ellipsis rather than pushing the length
    // off the bar.
    const auto font = LnF::uiFont (14.0f);
    const auto length = formatLength (lengthSeconds);
    const float gapW    = juce::GlyphArrangement::getStringWidth (font, "  ");
    const float lengthW = juce::GlyphArrangement::getStringWidth (font, length);
    const float maxNameW = juce::jmax (0.0f, (float) getWidth() - 80.0f - gapW - lengthW);
    const float nameW   = juce::jmin (maxNameW, juce::GlyphArrangement::getStringWidth (font, fileName) + 1.0f);

    auto row = getLocalBounds().toFloat().withSizeKeepingCentre (nameW + gapW + lengthW, (float) getHeight());

    g.setFont (font);
    g.setColour (LnF::ink());
    g.drawText (fileName, row.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    row.removeFromLeft (gapW);
    g.setColour (LnF::ink().withAlpha (0.45f));
    g.drawText (length, row, juce::Justification::centredLeft, false);
}
