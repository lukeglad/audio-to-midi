#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    The 20 px top bar: the only fixed furniture (house rule). Global things
    only. So far that is the loaded file's name and length, centred; the
    model picker and "?" guide arrive in later build steps.
*/
class TopBar : public juce::Component
{
public:
    TopBar();

    /** Empty name hides the readout. */
    void setFile (const juce::String& fileName, double lengthSeconds);

    void paint (juce::Graphics&) override;

private:
    juce::String fileName;
    double lengthSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
