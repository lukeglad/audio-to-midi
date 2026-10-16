#pragma once

#include <JuceHeader.h>
#include "AudioFlowerIcons.h"

//==============================================================================
/**
    The 20 px top bar: the only fixed furniture (house rule). Global things
    only: the "?" at the far left, the model control at the far right, and
    in the centre the loaded file's name and length, kept clear of both, or
    the copper recording readout. (Play and record live in the waveform's
    corner; the mix and the tempo in pills at the roll's bottom corners.)

    The model control is a button; the editor builds and shows its menu, since
    the menu's contents depend on download state the bar doesn't own.
*/
class TopBar : public juce::Component
{
public:
    TopBar();

    /** Empty name hides the readout. */
    void setFile (const juce::String& fileName, double lengthSeconds);

    /** The model control's text: "model", or "model · 43%" while one downloads. */
    void setModelLabel (const juce::String&);

    /** Shown in copper in place of the file readout while non-empty:
        "recording · from bar 9 · 0:04". */
    void setLiveText (const juce::String&);

    juce::Component& getModelControl() noexcept { return modelButton; }
    std::function<void()> onModelClicked;

    /** The "?" at the far left: opens the quick guide. */
    std::function<void()> onHelpClicked;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Text with an outline chevron after it, in the dropdown style the house
    // LookAndFeel gives a ComboBox. Resting ink 75%, hovered 100%.
    struct MenuButton : juce::Button
    {
        MenuButton() : juce::Button ("menu") {}
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
        int getIdealWidth() const;
    };

    // An outline Tabler glyph, resting 75%, hovered 100%.
    struct IconButton : juce::Button
    {
        explicit IconButton (const juce::String& paths);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
        std::unique_ptr<juce::Drawable> icon;
    };

    juce::String fileName, liveText;
    double lengthSeconds = 0.0;
    MenuButton modelButton;
    IconButton helpButton { AudioFlowerIcons::kHelp };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
