#include "DragValue.h"
#include "AudioFlowerLookAndFeel.h"

using LnF = AudioFlowerLookAndFeel;

DragValue::DragValue()
{
    setEditable (false, true, false);   // double-click to type
    setJustificationType (juce::Justification::centredLeft);
    setBorderSize (juce::BorderSize<int> (0));
    setMinimumHorizontalScale (1.0f);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    setColour (juce::Label::textColourId, LnF::ink());
    setColour (juce::Label::textWhenEditingColourId, LnF::ink());
    setColour (juce::Label::backgroundWhenEditingColourId, LnF::ink().withAlpha (0.10f));
    setColour (juce::Label::outlineWhenEditingColourId, LnF::ink().withAlpha (0.40f));
    setColour (juce::TextEditor::highlightColourId, LnF::ink().withAlpha (0.25f));
    setColour (juce::CaretComponent::caretColourId, LnF::ink());

    format = [] (double v) { return juce::String (v, 1); };
    parse  = [] (const juce::String& text) { return text.trim().getDoubleValue(); };
}

void DragValue::setRange (double min, double max, double perPixel)
{
    minimum = min;
    maximum = max;
    unitsPerPixel = perPixel;
    setValue (value, juce::dontSendNotification);
}

void DragValue::setValue (double newValue, juce::NotificationType notify)
{
    newValue = juce::jlimit (minimum, maximum, newValue);
    const bool changed = ! juce::exactlyEqual (newValue, value);
    value = newValue;
    refreshText();

    if (changed && notify != juce::dontSendNotification && onValueChanged)
        onValueChanged (value);
}

void DragValue::refreshText()
{
    if (! isBeingEdited())
        setText (format ? format (value) : juce::String (value), juce::dontSendNotification);
}

void DragValue::mouseDown (const juce::MouseEvent& e)
{
    juce::Label::mouseDown (e);
    dragging = false;
    dragStartValue = value;
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (isBeingEdited())
        return;

    dragging = true;
    const double fine = e.mods.isShiftDown() ? 0.1 : 1.0;
    setValue (dragStartValue - e.getDistanceFromDragStartY() * unitsPerPixel * fine, juce::sendNotification);
}

void DragValue::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        juce::Label::mouseUp (e);
    dragging = false;
}

void DragValue::editorShown (juce::TextEditor* editor)
{
    editor->setText (juce::String (value, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), false);
    editor->setFont (LnF::uiFont (getFont().getHeight()));
    editor->setJustification (juce::Justification::centredLeft);
    editor->selectAll();
}

void DragValue::textWasEdited()
{
    const double parsed = parse ? parse (getText()) : getText().getDoubleValue();

    if (std::isfinite (parsed))
        setValue (parsed, juce::sendNotification);
    else
        refreshText();
}
