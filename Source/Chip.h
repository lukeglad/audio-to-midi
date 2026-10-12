#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    A worded button in the house "chip" style (ReferenceSheet section 05):
    a 20 px stadium with a 1 px ink-45% hairline and 12 px lowercase text,
    filling with ink (ground-coloured text) while hovered or held. Worded
    rather than a glyph, per the settled rule that ambiguous icons lose to
    words.
*/
class Chip : public juce::Button
{
public:
    explicit Chip (const juce::String& text = {});

    void setText (const juce::String&);

    /** Text width plus padding; the caller sizes the chip with it. */
    int getIdealWidth() const;

    static constexpr int kHeight = 20;

private:
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chip)
};
