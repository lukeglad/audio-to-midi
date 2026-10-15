#include "QuantizeControl.h"
#include "GlassMenu.h"
#include "AudioFlowerLookAndFeel.h"
#include "Glass.h"

using LnF = AudioFlowerLookAndFeel;

namespace
{
    constexpr float kTextSize = 12.0f;
    constexpr int   kPadX = 10;
    constexpr int   kGap = 10;          // the same between "quantize", the value and the grid
    constexpr int   kChevronW = 12;     // the grid's chevron, after its text

    float textWidth (const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (LnF::uiFont (kTextSize), text);
    }

    int widthOf (const juce::String& text) { return (int) std::ceil (textWidth (text)); }

    // The value's slot is as wide as its widest text, "100%", with the value
    // centred in it: any spare room splits equally either side, so the two
    // gaps stay even and nothing shifts as the digits change.
    int valueSlotWidth() { return widthOf ("100%"); }
}

int QuantizeLabel::getIdealWidth() const
{
    return kPadX + widthOf ("quantize") + kGap + valueSlotWidth() + kGap
         + widthOf (atm::gridName (grid)) + kChevronW + kPadX;
}

//==============================================================================
QuantizeLabel::QuantizeLabel()
    : surface (LnF::background())
{
    percent.setRange (0.0, 100.0, 0.5);
    percent.format = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    percent.parse = [] (const juce::String& text)
    {
        const auto digits = text.retainCharacters ("0123456789.");
        return digits.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : digits.getDoubleValue();
    };
    percent.onValueChanged = [this] (double v) { if (onAmountChanged) onAmountChanged (v / 100.0); };
    percent.setFont (LnF::uiFont (kTextSize));
    percent.setJustificationType (juce::Justification::centred);

    // The percentage's hover is what raises the fader; listen to it directly,
    // since the label itself doesn't see mouse events over its child.
    percent.addMouseListener (this, false);
    addAndMakeVisible (percent);

    setValues (0.0, grid);
}

void QuantizeLabel::setValues (double amount01, atm::Grid newGrid)
{
    const int widthBefore = getIdealWidth();

    if (! percent.isBeingEdited())
        percent.setValue (amount01 * 100.0, juce::dontSendNotification);

    if (newGrid != grid)
    {
        grid = newGrid;
        repaint();
    }

    if (getIdealWidth() != widthBefore)
    {
        resized();
        if (onWidthChanged)
            onWidthChanged();
    }
}

void QuantizeLabel::setSurfaceColour (juce::Colour c)
{
    if (c != surface) { surface = c; repaint(); }
}

void QuantizeLabel::resized()
{
    // Laid out from the measured text: caption, gap, value, gap, grid.
    percent.setBounds (kPadX + widthOf ("quantize") + kGap, 0, valueSlotWidth(), getHeight());
}

juce::Rectangle<int> QuantizeLabel::gridBounds() const
{
    const int x = kPadX + widthOf ("quantize") + kGap + valueSlotWidth() + kGap;
    return { x, 0, widthOf (atm::gridName (grid)) + kChevronW, getHeight() };
}

void QuantizeLabel::paint (juce::Graphics& g)
{
    // Liquid glass, like every floating surface; the faint flat plate is
    // the fallback when there's nothing to read through.
    const auto r = getLocalBounds().toFloat();
    if (! Glass::paint (g, *this, r, surface))
    {
        g.setColour (surface.withAlpha (0.88f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    }

    g.setFont (LnF::uiFont (kTextSize));
    g.setColour (LnF::ink().withAlpha (0.45f));
    g.drawText ("quantize", juce::Rectangle<int> (kPadX, 0, widthOf ("quantize") + 1, getHeight()), juce::Justification::centredLeft, false);

    // The grid, in the house dropdown style: text and a chevron.
    const auto gb = gridBounds();
    const bool hot = menuOpen || (isMouseOver() && gb.contains (getMouseXYRelative()));
    g.setColour (LnF::ink().withAlpha (hot ? 1.0f : 0.75f));
    const auto name = atm::gridName (grid);
    g.drawText (name, gb, juce::Justification::centredLeft, false);

    const float w = 7.0f, h = 3.5f;
    const float cx = (float) gb.getRight() - w * 0.5f, cy = (float) gb.getCentreY() + 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - w * 0.5f, cy - h * 0.5f);
    chevron.lineTo          (cx,            cy + h * 0.5f);
    chevron.lineTo          (cx + w * 0.5f, cy - h * 0.5f);
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void QuantizeLabel::mouseEnter (const juce::MouseEvent& e)
{
    if (e.eventComponent == &percent && onPercentHovered)
        onPercentHovered();
    repaint();
}

void QuantizeLabel::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && gridBounds().contains (e.getPosition()))
        showGridMenu();
}

void QuantizeLabel::showGridMenu()
{
    auto* menu = GlassMenu::find (*this);
    if (menu == nullptr)
        return;

    GlassMenu::Items items;
    for (const auto g : { atm::Grid::sixteenth, atm::Grid::eighth, atm::Grid::quarter })
        items.push_back ({ (int) g, atm::gridName (g), true, g == grid });

    menuOpen = true;
    repaint();

    juce::Component::SafePointer<QuantizeLabel> safeThis (this);
    menu->showBelow (*this, std::move (items), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        safeThis->menuOpen = false;
        safeThis->repaint();
        if (result != 0 && safeThis->onGridChanged)
            safeThis->onGridChanged ((atm::Grid) result);
    }, true);
}

//==============================================================================
QuantizeFader::QuantizeFader()
    : surface (LnF::background())
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void QuantizeFader::setValue (double amount01)
{
    amount01 = juce::jlimit (0.0, 1.0, amount01);
    if (! juce::exactlyEqual (amount01, value)) { value = amount01; repaint(); }
}

void QuantizeFader::setSurfaceColour (juce::Colour c)
{
    if (c != surface) { surface = c; repaint(); }
}

juce::Rectangle<float> QuantizeFader::body() const
{
    return getLocalBounds().withTrimmedBottom (kGap).toFloat().reduced (0.5f);
}

juce::Rectangle<float> QuantizeFader::track() const
{
    return body().withSizeKeepingCentre (6.0f, body().getHeight() - 18.0f);
}

void QuantizeFader::paint (juce::Graphics& g)
{
    const auto b = body();
    if (! Glass::paint (g, *this, b, surface))
    {
        g.setColour (surface);
        g.fillRoundedRectangle (b, b.getWidth() * 0.5f);
        g.setColour (LnF::ink().withAlpha (0.40f));
        g.drawRoundedRectangle (b, b.getWidth() * 0.5f, 1.0f);
    }

    const auto t = track();
    g.setColour (LnF::ink().withAlpha (0.40f));
    g.drawRoundedRectangle (t, t.getWidth() * 0.5f, 1.0f);

    // Filled from the bottom, like a level.
    const float filled = t.getHeight() * (float) value;
    if (filled > 0.5f)
    {
        g.setColour (LnF::ink().withAlpha (0.85f));
        g.fillRoundedRectangle (t.withTop (t.getBottom() - filled), t.getWidth() * 0.5f);
    }
}

void QuantizeFader::setFromY (float y)
{
    const auto t = track();
    const double v = juce::jlimit (0.0, 1.0, (double) ((t.getBottom() - y) / t.getHeight()));
    setValue (v);
    if (onChanged)
        onChanged (v);
}

void QuantizeFader::mouseDown (const juce::MouseEvent& e)
{
    setFromY (e.position.y);
}

void QuantizeFader::mouseDrag (const juce::MouseEvent& e)
{
    setFromY (e.position.y);
}
