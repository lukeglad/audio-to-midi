#include "Chip.h"
#include "AudioFlowerLookAndFeel.h"
#include "Glass.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 12.0f;
    constexpr int   kPaddingX = 10;
}

Chip::Chip (const juce::String& text)
    : juce::Button (text)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Chip::setText (const juce::String& text)
{
    setButtonText (text);
}

int Chip::getIdealWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), getButtonText()))
           + kPaddingX * 2;
}

void Chip::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    // Hovered, held, or switched on (a toggling chip): the house "on" fill.
    const bool hot = isMouseOver || isButtonDown || getToggleState();

    if (hot)
    {
        g.setColour (LnF::ink());
        g.fillRoundedRectangle (r, radius);
        g.setColour (LnF::background());
    }
    else
    {
        // Glass where there's something to float over (the canvas); a plain
        // hairline elsewhere.
        if (! Glass::paint (g, *this, getLocalBounds().toFloat(), LnF::background()))
        {
            g.setColour (LnF::ink().withAlpha (0.45f));
            g.drawRoundedRectangle (r, radius, 1.0f);
        }
        g.setColour (LnF::ink());
    }

    g.setFont (LnF::uiFont (kTextSize));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}
