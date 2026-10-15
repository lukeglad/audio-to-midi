#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"

//==============================================================================
/**
    The record control, under play in the waveform band's corner: a filled
    dot (the transport's filled-glyph exception, like play and stop). Ink at
    rest; copper once armed; copper breathing while a recording runs. Red
    would be the convention, but the house has no red: copper already means
    "live" here (the playhead).
*/
class RecordButton : public juce::Button,
                     private juce::Timer
{
public:
    enum class State { idle, armed, recording };

    RecordButton() : juce::Button ("record")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setState (State newState)
    {
        if (newState == state)
            return;

        state = newState;
        if (state == State::recording)
            startTimerHz (30);
        else
            stopTimer();
        repaint();
    }

    static constexpr float kDotSize = 15.0f;

private:
    void timerCallback() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto dot = getLocalBounds().toFloat().withSizeKeepingCentre (kDotSize, kDotSize);
        const bool hot = isMouseOver || isButtonDown;

        if (state == State::idle)
        {
            g.setColour (AudioFlowerLookAndFeel::ink().withAlpha (hot ? 1.0f : 0.75f));
        }
        else if (state == State::armed)
        {
            g.setColour (AudioFlowerLookAndFeel::accent());
        }
        else
        {
            // A slow breath, 0.55 to 1: alive, not alarming.
            const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            const float breath = 0.55f + 0.45f * (float) (0.5 + 0.5 * std::sin (t * 2.0 * juce::MathConstants<double>::pi / 1.6));
            g.setColour (AudioFlowerLookAndFeel::accent().withAlpha (breath));
        }

        g.fillEllipse (dot);
    }

    State state = State::idle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordButton)
};
