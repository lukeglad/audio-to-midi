#include "TopBar.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 14.0f;
    constexpr int   kSideInset = 10;
    constexpr int   kGroupGap = 6;

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

int TopBar::MenuButton::getIdealWidth() const
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

    // Tempo: 20-400 BPM, a quarter of a BPM per pixel (shift for a fortieth).
    bpmValue.setRange (20.0, 400.0, 0.25);
    bpmValue.format = [] (double bpm)
    {
        // Whole numbers stay whole; a detected 92.35 shows as such.
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.005;
        return (whole ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 2)) + " BPM";
    };
    bpmValue.parse = [] (const juce::String& text)
    {
        const auto digits = text.retainCharacters ("0123456789.");
        return digits.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : digits.getDoubleValue();
    };
    bpmValue.onValueChanged = [this] (double bpm) { if (onBpmChanged) onBpmChanged (bpm); };
    bpmValue.setFont (LnF::uiFont (kTextSize));
    addChildComponent (bpmValue);

    halfChip.onClick   = [this] { if (onHalve)  onHalve(); };
    doubleChip.onClick = [this] { if (onDouble) onDouble(); };

    for (auto* chip : { &halfChip, &doubleChip })
        addChildComponent (chip);

    // Quantize: 0-100 %, half a percent per pixel, read as "quantize 50%".
    quantizeValue.setRange (0.0, 100.0, 0.5);
    quantizeValue.format = [] (double v) { return "quantize " + juce::String (juce::roundToInt (v)) + "%"; };
    quantizeValue.parse = [] (const juce::String& text)
    {
        const auto digits = text.retainCharacters ("0123456789.");
        return digits.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : digits.getDoubleValue();
    };
    quantizeValue.onValueChanged = [this] (double v) { if (onQuantizeChanged) onQuantizeChanged (v / 100.0); };
    quantizeValue.setFont (LnF::uiFont (kTextSize));
    addChildComponent (quantizeValue);

    gridButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    gridButton.onClick = [this] { showGridMenu(); };
    addChildComponent (gridButton);
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

void TopBar::setTempo (bool has, double bpm)
{
    hasTempo = has;

    for (auto* c : std::initializer_list<juce::Component*> { &bpmValue, &halfChip, &doubleChip, &quantizeValue, &gridButton })
        c->setVisible (has);

    if (! bpmValue.isBeingEdited())
        bpmValue.setValue (bpm, juce::dontSendNotification);

    resized();   // the readout's width follows its text
}

void TopBar::setQuantize (double amount01, atm::Grid newGrid)
{
    if (! quantizeValue.isBeingEdited())
        quantizeValue.setValue (amount01 * 100.0, juce::dontSendNotification);

    if (newGrid != grid || gridButton.getButtonText().isEmpty())
    {
        grid = newGrid;
        gridButton.setButtonText (atm::gridName (grid));
    }

    resized();
}

void TopBar::showGridMenu()
{
    juce::PopupMenu menu;
    for (const auto g : { atm::Grid::sixteenth, atm::Grid::eighth, atm::Grid::quarter })
        menu.addItem ((int) g, atm::gridName (g), true, g == grid);

    juce::Component::SafePointer<TopBar> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (gridButton), [safeThis] (int result)
    {
        if (safeThis != nullptr && result != 0 && safeThis->onGridChanged)
            safeThis->onGridChanged ((atm::Grid) result);
    });
}

void TopBar::resized()
{
    modelButton.setBounds (getLocalBounds().removeFromRight (modelButton.getIdealWidth() + kSideInset)
                               .withTrimmedRight (kSideInset));

    auto left = getLocalBounds().withTrimmedLeft (kSideInset);
    const int bpmW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), bpmValue.getText())) + 6;
    bpmValue.setBounds (left.removeFromLeft (bpmW));
    left.removeFromLeft (kGroupGap);

    for (auto* chip : { &halfChip, &doubleChip })
    {
        chip->setBounds (left.removeFromLeft (chip->getIdealWidth()).withSizeKeepingCentre (chip->getIdealWidth(), Chip::kHeight));
        left.removeFromLeft (kGroupGap);
    }

    // The quantize group, a wider gap on from the tempo group.
    left.removeFromLeft (kGroupGap * 2);
    const int quantizeW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), quantizeValue.getText())) + 6;
    quantizeValue.setBounds (left.removeFromLeft (quantizeW));
    left.removeFromLeft (kGroupGap);
    gridButton.setBounds (left.removeFromLeft (gridButton.getIdealWidth()));
}

void TopBar::paint (juce::Graphics& g)
{
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
    const float leftEdge  = (float) (hasTempo ? gridButton.getRight() : 0) + (float) kSideInset;
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
