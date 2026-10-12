#include "TopBar.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 14.0f;
    constexpr int   kSideInset = 10;

    juce::String formatLength (double seconds)
    {
        const int total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
void TopBar::ModelButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto r = getLocalBounds().toFloat();
    const float alpha = (isMouseOver || isButtonDown) ? 1.0f : 0.75f;

    g.setColour (LnF::ink().withAlpha (alpha));
    g.setFont (LnF::uiFont (kTextSize));
    g.drawText (getButtonText(), r.withTrimmedRight (14.0f), juce::Justification::centredLeft, false);

    // Tabler chevron-down proportions, same 1.2 px stroke as the house ComboBox.
    const float w = 8.0f, h = 4.0f;
    const float cx = r.getRight() - 6.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - w * 0.5f, cy - h * 0.5f);
    chevron.lineTo          (cx,            cy + h * 0.5f);
    chevron.lineTo          (cx + w * 0.5f, cy - h * 0.5f);
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

int TopBar::ModelButton::getIdealWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), getButtonText())) + 16;
}

//==============================================================================
TopBar::TopBar()
{
    setInterceptsMouseClicks (false, true);

    modelButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    modelButton.onClick = [this] { if (onModelClicked) onModelClicked(); };
    addAndMakeVisible (modelButton);
}

void TopBar::setFile (const juce::String& name, double seconds)
{
    if (name == fileName && juce::exactlyEqual (seconds, lengthSeconds))
        return;

    fileName = name;
    lengthSeconds = seconds;
    repaint();
}

void TopBar::setModelLabel (const juce::String& label)
{
    if (label == modelButton.getButtonText())
        return;

    modelButton.setButtonText (label);
    resized();
    modelButton.repaint();
}

void TopBar::resized()
{
    modelButton.setBounds (getLocalBounds().removeFromRight (modelButton.getIdealWidth() + kSideInset)
                               .withTrimmedRight (kSideInset));
}

void TopBar::paint (juce::Graphics& g)
{
    if (fileName.isEmpty())
        return;

    // Name at full ink, length dimmed after it, the pair centred on the bar
    // the way EQ centres its preset name. Measured in the same 14 px face,
    // so a long name is cut with an ellipsis rather than pushing the length
    // off the bar or under the model control.
    const auto font = LnF::uiFont (kTextSize);
    const auto length = formatLength (lengthSeconds);
    const float gapW    = juce::GlyphArrangement::getStringWidth (font, "  ");
    const float lengthW = juce::GlyphArrangement::getStringWidth (font, length);
    const float sideReserve = (float) (modelButton.getWidth() + kSideInset * 2);
    const float maxNameW = juce::jmax (0.0f, (float) getWidth() - 2.0f * sideReserve - gapW - lengthW);
    const float nameW   = juce::jmin (maxNameW, juce::GlyphArrangement::getStringWidth (font, fileName) + 1.0f);

    auto row = getLocalBounds().toFloat().withSizeKeepingCentre (nameW + gapW + lengthW, (float) getHeight());

    g.setFont (font);
    g.setColour (LnF::ink());
    g.drawText (fileName, row.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    row.removeFromLeft (gapW);
    g.setColour (LnF::ink().withAlpha (0.45f));
    g.drawText (length, row, juce::Justification::centredLeft, false);
}
