#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    A number you drag up and down, or double-click to type, drawn as plain
    text in the house face (EQ's readout convention: no box, no arrows).
    Shift while dragging for fine steps.

    `format` turns the value into display text ("92 BPM"); `parse` turns typed
    text back into a value, or NaN to reject it. The editor opens with the
    bare number.
*/
class DragValue : public juce::Label
{
public:
    DragValue();

    void setRange (double min, double max, double unitsPerPixel);
    void setValue (double newValue, juce::NotificationType);
    double getValue() const noexcept { return value; }

    std::function<juce::String (double)> format;
    std::function<double (const juce::String&)> parse;
    std::function<void (double)> onValueChanged;

    /** A readout only: no drag, no typing (a recording's tempo is the host's). */
    void setDraggable (bool shouldDrag)
    {
        setInterceptsMouseClicks (shouldDrag, shouldDrag);
        setMouseCursor (shouldDrag ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void editorShown (juce::TextEditor*) override;
    void editorAboutToBeHidden (juce::TextEditor*) override;
    void paintOverChildren (juce::Graphics&) override;
    void textWasEdited() override;

private:
    void refreshText();

    double value = 0.0, minimum = 0.0, maximum = 1.0, unitsPerPixel = 1.0;
    double dragStartValue = 0.0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragValue)
};
