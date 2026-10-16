#pragma once

#include <JuceHeader.h>
#include "AudioFlowerLookAndFeel.h"
#include "Glass.h"

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

    static constexpr float kDotSize = 10.0f;   // in its pill, under play's

    void setSurfaceColour (juce::Colour c) { if (c != surface) { surface = c; repaint(); } }

private:
    void timerCallback() override { repaint(); }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        // Its own glass pill, which carries the state too: a copper rim when
        // armed, a copper tint as well while recording.
        const auto pill = getLocalBounds().toFloat();
        const auto rim = state == State::idle ? std::optional<juce::Colour>() : std::optional (AudioFlowerLookAndFeel::accent());
        if (! Glass::paint (g, *this, pill, surface, rim))
        {
            g.setColour (surface);
            g.fillRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f);
            g.setColour (rim.value_or (AudioFlowerLookAndFeel::ink().withAlpha (0.40f)));
            g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
        }
        if (state == State::recording)
        {
            g.setColour (AudioFlowerLookAndFeel::accent().withAlpha (0.18f));
            g.fillRoundedRectangle (pill.reduced (1.0f), pill.getHeight() * 0.5f - 1.0f);
        }

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
    juce::Colour surface { AudioFlowerLookAndFeel::background() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordButton)
};
