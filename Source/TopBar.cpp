#include "TopBar.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 14.0f;
    constexpr int   kSideInset = 10;
    constexpr int   kHelpCentreX = 16;   // "?" ~13 px in from the left: mirrors the model chevron's inset on the right

    juce::String formatLength (double seconds)
    {
        const int total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
void TopBar::MenuButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto r = getLocalBounds().toFloat();
    const float alpha = (isMouseOver || isButtonDown) ? 1.0f : 0.75f;

    const auto font = LnF::uiFont (kTextSize);
    g.setColour (LnF::ink().withAlpha (alpha));
    g.setFont (font);
    g.drawText (getButtonText(), r.withTrimmedRight (14.0f), juce::Justification::centredLeft, false);

    // Tabler chevron-down proportions, same 1.2 px stroke as the house
    // ComboBox, sized and centred for LOWERCASE text: "model" has no tall
    // letters, so a chevron centred on the whole line sat level with the
    // tops of the letters and read as too big. Centred on an "x" instead.
    juce::GlyphArrangement x;
    x.addLineOfText (font, "x", 0.0f, 0.0f);
    const auto xBox = x.getBoundingBox (0, 1, true);
    const float baseline = r.getCentreY() - font.getHeight() * 0.5f + font.getAscent();
    const float w = 6.5f, h = 3.25f;
    const float cx = r.getRight() - 6.0f, cy = baseline + xBox.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - w * 0.5f, cy - h * 0.5f);
    chevron.lineTo          (cx,            cy + h * 0.5f);
    chevron.lineTo          (cx + w * 0.5f, cy - h * 0.5f);
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

int TopBar::MenuButton::getIdealWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), getButtonText())) + 16;
}

//==============================================================================
TopBar::IconButton::IconButton (const juce::String& paths)
    : juce::Button ("icon"), icon (AudioFlowerIcons::makeTablerIcon (paths))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TopBar::IconButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    if (icon != nullptr)
        // 11 px tall, the height of the bar's capitals; drawWithin fits the
        // glyph's own outline, so a 14 px box made it taller than the text.
        icon->drawWithin (g, getLocalBounds().toFloat().withSizeKeepingCentre (11.0f, 11.0f),
                          juce::RectanglePlacement::centred, (isMouseOver || isButtonDown) ? 1.0f : 0.75f);
}

//==============================================================================
TopBar::TopBar()
{
    helpButton.setTooltip ("quick guide");
    helpButton.onClick = [this] { if (onHelpClicked) onHelpClicked(); };
    addAndMakeVisible (helpButton);

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
    // The "?" at the far left and the model control at the far right, the
    // same distance in from each edge (user's pick over centring the "?"
    // on the play and record pills, mockups/help-placement.html).
    helpButton.setBounds (kHelpCentreX - 9, 0, 18, getHeight());
    modelButton.setBounds (getWidth() - kSideInset - modelButton.getIdealWidth(), 0,
                           modelButton.getIdealWidth(), getHeight());

}

void TopBar::setLiveText (const juce::String& text)
{
    if (text == liveText)
        return;

    liveText = text;
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    if (liveText.isNotEmpty())
    {
        // Centred like the file readout, and kept clear of the controls
        // either side on a narrow window: cut with an ellipsis if it must.
        const auto font = LnF::uiFont (kTextSize);
        const float leftEdge  = (float) (helpButton.getRight() + kSideInset);
        const float rightEdge = (float) (modelButton.getX() - kSideInset);
        const float textW     = juce::jmin (rightEdge - leftEdge, juce::GlyphArrangement::getStringWidth (font, liveText) + 1.0f);
        const float x         = juce::jlimit (leftEdge, juce::jmax (leftEdge, rightEdge - textW), ((float) getWidth() - textW) * 0.5f);

        g.setFont (font);
        g.setColour (LnF::accent());
        g.drawText (liveText, juce::Rectangle<float> (x, 0.0f, juce::jmax (0.0f, textW), (float) getHeight()),
                    juce::Justification::centredLeft, true);
        return;
    }

    if (fileName.isEmpty())
        return;

    // Name at full ink, length dimmed after it, the pair centred on the bar
    // the way EQ centres its preset name. Measured in the same 14 px face,
    // so a long name is cut with an ellipsis rather than running into the
    // controls either side.
    const auto font = LnF::uiFont (kTextSize);
    const auto length = formatLength (lengthSeconds);
    const float gapW    = juce::GlyphArrangement::getStringWidth (font, "  ");
    const float lengthW = juce::GlyphArrangement::getStringWidth (font, length);

    // Centred on the bar when it fits, otherwise centred in the space left
    // between the controls either side, the name cut with an ellipsis.
    const float leftEdge  = (float) (helpButton.getRight() + kSideInset);
    const float rightEdge = (float) (modelButton.getX() - kSideInset);
    const float maxNameW  = juce::jmax (0.0f, rightEdge - leftEdge - gapW - lengthW);
    const float nameW     = juce::jmin (maxNameW, juce::GlyphArrangement::getStringWidth (font, fileName) + 1.0f);
    const float rowW      = nameW + gapW + lengthW;
    const float rowX      = juce::jlimit (leftEdge, juce::jmax (leftEdge, rightEdge - rowW), ((float) getWidth() - rowW) * 0.5f);

    auto row = juce::Rectangle<float> (rowX, 0.0f, rowW, (float) getHeight());

    g.setFont (font);
    g.setColour (LnF::ink());
    g.drawText (fileName, row.removeFromLeft (nameW), juce::Justification::centredLeft, true);

    row.removeFromLeft (gapW);
    g.setColour (LnF::ink().withAlpha (0.45f));
    g.drawText (length, row, juce::Justification::centredLeft, false);
}
