#pragma once

#include <JuceHeader.h>
#include "DragValue.h"
#include "Engine/Cleanup.h"

//==============================================================================
/**
    The grid pill, at the bottom-right of the piano roll: everything that
    shapes the grid, together:

        126 BPM  ·  quantize  50%  1/16 ⌄

    Liquid glass, so notes passing behind don't make it unreadable. The
    tempo and the percentage drag up/down or double-click to type (a
    recording's tempo is the host's and only reads); the grid opens a glass
    menu.

    Hovering the percentage raises a QuantizeFader above it (the canvas owns
    and positions that, since it reaches up over the roll).
*/
class QuantizeLabel : public juce::Component
{
public:
    QuantizeLabel();

    void setValues (double amount01, atm::Grid);

    /** The tempo at the pill's left. `editable` false: a readout only. */
    void setTempo (double bpm, bool editable);
    std::function<void (double bpm)> onTempoChanged;
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
    int tempoWidth() const;
    int quantizeX() const;   // where "quantize" starts, after the tempo and the dot
    void showGridMenu();

    DragValue tempo;
    DragValue percent;
    atm::Grid grid = atm::Grid::sixteenth;
    juce::Colour surface;
    bool menuOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuantizeLabel)
};

//==============================================================================
/**
    The fader that rises above the quantize percentage on hover: a slim
    liquid-glass stadium (a flat one without a backdrop) with a track that
    fills from the bottom like a meter, so it doesn't read as a scrollbar. Drag it, or
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
