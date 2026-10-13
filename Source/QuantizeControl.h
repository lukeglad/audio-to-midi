#pragma once

#include <JuceHeader.h>
#include "DragValue.h"
#include "Engine/Cleanup.h"

//==============================================================================
/**
    Quantize, at the bottom-right of the piano roll: a quiet label

        quantize  50%  1/16 ⌄

    on a faint surface-shaded backing (no hairline), so notes passing behind
    don't make it unreadable. The percentage drags up/down or double-clicks
    to type; the grid opens the house popup menu.

    Hovering the percentage raises a QuantizeFader above it (the canvas owns
    and positions that, since it reaches up over the roll). Chosen from
    mockups/quantize-hover-fader.html on 2026-10-13.
*/
class QuantizeLabel : public juce::Component
{
public:
    QuantizeLabel();

    void setValues (double amount01, atm::Grid);
    void setSurfaceColour (juce::Colour);

    /** Where the percentage sits, in the label's coordinates: the fader
        rises from here. */
    juce::Rectangle<int> getPercentBounds() const { return percent.getBounds(); }

    std::function<void (double amount01)> onAmountChanged;
    std::function<void (atm::Grid)> onGridChanged;
    std::function<void()> onPercentHovered;

    /** The width its current text needs: measured, so the three parts sit
        exactly one gap apart. Changes when the value's digit count does. */
    int getIdealWidth() const;
    static constexpr int kHeight = 22;

    /** Called when getIdealWidth() changes, so the canvas can re-anchor it. */
    std::function<void()> onWidthChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> gridBounds() const;
    void showGridMenu();

    DragValue percent;
    atm::Grid grid = atm::Grid::sixteenth;
    juce::Colour surface;
    bool menuOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuantizeLabel)
};

//==============================================================================
/**
    The fader that rises above the quantize percentage on hover: a slim house
    stadium (40% ink hairline, surface-shaded) with a track that fills from
    the bottom like a meter, so it doesn't read as a scrollbar. Drag it, or
    click a height to jump there.

    Its bounds include the gap down to the label, so the pointer never
    crosses empty space moving between them.
*/
class QuantizeFader : public juce::Component
{
public:
    QuantizeFader();

    void setValue (double amount01);
    void setSurfaceColour (juce::Colour);

    std::function<void (double amount01)> onChanged;

    static constexpr int kWidth = 22, kHeight = 100, kGap = 5;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> body() const;
    juce::Rectangle<float> track() const;
    void setFromY (float y);

    double value = 0.0;
    juce::Colour surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuantizeFader)
};
