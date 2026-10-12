#pragma once

#include <JuceHeader.h>
#include "Chip.h"
#include "DragValue.h"
#include "Engine/Cleanup.h"

//==============================================================================
/**
    The 20 px top bar: the only fixed furniture (house rule). Global things
    only. Left: the tempo group (BPM readout, half / double) and then the
    quantize group (amount, grid), both shown once a tempo exists. Centre:
    the loaded file's name and length, kept clear of both sides. Right: the
    model control. The "?" guide arrives in a later build step.

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

    /** The tempo and quantize groups. `hasTempo` false hides both. */
    void setTempo (bool hasTempo, double bpm);
    void setQuantize (double amount01, atm::Grid);

    juce::Component& getModelControl() noexcept { return modelButton; }
    std::function<void()> onModelClicked;

    std::function<void (double)> onBpmChanged;
    std::function<void()> onHalve, onDouble;
    std::function<void (double)> onQuantizeChanged;   // 0..1
    std::function<void (atm::Grid)> onGridChanged;

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

    juce::String fileName;
    double lengthSeconds = 0.0;
    MenuButton modelButton;

    void showGridMenu();

    DragValue bpmValue, quantizeValue;
    MenuButton gridButton;
    atm::Grid grid = atm::Grid::sixteenth;
    Chip halfChip { juce::String (juce::CharPointer_UTF8 ("\xC2\xBD")) };
    Chip doubleChip { juce::String (juce::CharPointer_UTF8 ("\xC3\x97" "2")) };
    bool hasTempo = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
