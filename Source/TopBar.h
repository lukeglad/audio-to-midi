#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    The 20 px top bar: the only fixed furniture (house rule). Global things
    only: the loaded file's name and length centred, and the model control
    at the right. The "?" guide arrives in a later build step.

    The model control is a button; the editor builds and shows its menu, since
    the menu's contents depend on download state the bar doesn't own.
*/
class TopBar : public juce::Component
{
public:
    TopBar();

    /** Empty name hides the readout. */
    void setFile (const juce::String& fileName, double lengthSeconds);

    /** The model control's text, e.g. "medium" or "medium · 43%". */
    void setModelLabel (const juce::String&);

    juce::Component& getModelControl() noexcept { return modelButton; }
    std::function<void()> onModelClicked;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Text with an outline chevron after it, in the dropdown style the house
    // LookAndFeel gives a ComboBox. Resting ink 75%, hovered 100%.
    struct ModelButton : juce::Button
    {
        ModelButton() : juce::Button ("model") {}
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
        int getIdealWidth() const;
    };

    juce::String fileName;
    double lengthSeconds = 0.0;
    ModelButton modelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
