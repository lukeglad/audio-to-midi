#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    The preview mix, at the bottom-left of the piano roll, mirroring the grid
    pill at the bottom-right:

        audio  ━━━━━●  midi

    A crossfader, labelled at both ends so it says what it blends; the label
    nearer the dot is the brighter. Drag the dot or click the track; click a
    label to go all the way to it; double-click to return to the default,
    all midi. Liquid glass like the other floating surfaces.
*/
class MixPill : public juce::Component
{
public:
    MixPill();

    static constexpr float kDefault = 1.0f;   // all midi: what you came for
    static constexpr int kHeight = 22;

    void setValue (float mix01);
    void setSurfaceColour (juce::Colour);

    int getIdealWidth() const;

    std::function<void (float mix01)> onChanged;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Part { none, audio, track, midi };

    juce::Rectangle<float> audioBounds() const;
    juce::Rectangle<float> trackBounds() const;
    juce::Rectangle<float> midiBounds() const;
    Part partAt (juce::Point<float>) const;
    void set (float mix01);

    float value = kDefault;
    juce::Colour surface;
    Part hovered = Part::none;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixPill)
};
