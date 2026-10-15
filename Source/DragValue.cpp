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
    // Typing happens in place: no box, no outline, the same type where the
    // value was; a thin underline (paintOverChildren) says it's editable.
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, LnF::ink().withAlpha (0.18f));
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
    editor->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour (juce::TextEditor::textColourId, LnF::ink());
    editor->setColour (juce::TextEditor::highlightedTextColourId, LnF::ink());
    editor->setColour (juce::TextEditor::highlightColourId, LnF::ink().withAlpha (0.18f));
    editor->setColour (juce::CaretComponent::caretColourId, LnF::ink());

    // The same font, place and alignment as the value: no padding.
    editor->setBorder ({});
    editor->setIndents (0, 0);
    editor->setFont (LnF::uiFont (getFont().getHeight()));
    editor->setJustification (getJustificationType());

    editor->setText (juce::String (value, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), false);
    editor->selectAll();
    repaint();
}

void DragValue::editorAboutToBeHidden (juce::TextEditor*)
{
    repaint();
}

void DragValue::paintOverChildren (juce::Graphics& g)
{
    // While typing: a hairline under the text, the only sign of the field.
    if (isBeingEdited())
    {
        g.setColour (LnF::ink().withAlpha (0.5f));
        g.fillRect (0.0f, (float) getHeight() * 0.5f + getFont().getHeight() * 0.5f + 1.0f, (float) getWidth(), 1.0f);
    }
}

void DragValue::textWasEdited()
{
    const double parsed = parse ? parse (getText()) : getText().getDoubleValue();

    if (std::isfinite (parsed))
        setValue (parsed, juce::sendNotification);
    else
        refreshText();
}
