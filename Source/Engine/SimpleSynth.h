#pragma once

#include <juce_core/juce_core.h>

#include "NoteSynth.h"

#include <array>

namespace atm
{
    /**
        The preview's fallback synth, for when the soundfont isn't there:
        three harmonics with a short attack and decay for pitched notes, and
        drums shaped by their GM kind (kick sweep, snare noise + tone, hats
        and cymbals as differentiated noise, toms). Mono, centred.
    */
    class SimpleSynth : public NoteSynth
    {
    public:
        void noteOn (int layer, int pitch, bool drum, int velocity) override;
        void noteOff (int layer, int pitch, bool drum) override;
        void releaseAll() override;
        void cutAll() override;
        bool isSilent() const override;
        void render (float* left, float* right, int numSamples, double sampleRate) override;

    private:
        struct Voice
        {
            bool active = false;
            int layer = 0, pitch = 60;
            bool drum = false;
            int drumKind = 0;
            juce::uint32 order = 0;
            float gain = 1.0f;                  // from the velocity
            double phase = 0.0, freq = 440.0;   // phase: elapsed seconds
            double env = 0.0;
            bool released = false;
            juce::uint32 noise = 1;
        };

        static float renderVoice (Voice&, double sampleRate);

        std::array<Voice, 48> voices;
        juce::uint32 counter = 0;
    };
}
