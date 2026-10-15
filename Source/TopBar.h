#pragma once

#include <JuceHeader.h>
#include "Chip.h"
#include "DragValue.h"
#include "Engine/Cleanup.h"
#include "AudioFlowerIcons.h"

//==============================================================================
/**
    The 20 px top bar: the only fixed furniture (house rule). Global things
    only. Left: the preview mix (play lives in the waveform corner; the tempo
    moved to the grid pill at the roll's bottom-right on 2026-10-15). Centre:
    the loaded file's name and length, kept clear of both sides, or the
    copper recording readout. Right: the model control, then the "?".

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


    /** Shown in copper in place of the file readout while non-empty:
        "recording · from bar 9 · 0:04". */
    void setLiveText (const juce::String&);

    /** The preview mix: shown with a file loaded. */
    void setMix (bool hasFile, float mix01);
    std::function<void (float)> onMixChanged;   // 0..1

    juce::Component& getModelControl() noexcept { return modelButton; }
    std::function<void()> onModelClicked;

    /** The "?" left of the model control: opens the quick guide. */
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

    DragValue mixValue;
    bool hasFile = false;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
