#pragma once

namespace atm
{
    /**
        What the preview plays its notes through. The preview does the
        scheduling (which note starts or stops at which sample of a block);
        a synth only sounds what it is told, sample-accurately, because the
        preview renders the block in segments split at every event.

        Every method here is called on the audio thread, after prepare(), and
        must not allocate, lock or block.
    */
    class NoteSynth
    {
    public:
        virtual ~NoteSynth() = default;

        /** `layer` is the note's layer key (its GM program, or 128 for drums);
            `velocity` 1..127. */
        virtual void noteOn (int layer, int pitch, bool drum, int velocity) = 0;
        virtual void noteOff (int layer, int pitch, bool drum) = 0;

        /** Let everything sounding ring out through its release (a jump
            while playing, a stop): no step, no click. */
        virtual void releaseAll() = 0;

        /** Silence now (a seek while stopped, a new file). */
        virtual void cutAll() = 0;

        virtual bool isSilent() const = 0;

        /** Adds `numSamples` of stereo output at `sampleRate`. The rate is
            passed every time so a change of host rate never needs a
            separate, racy call. */
        virtual void render (float* left, float* right, int numSamples, double sampleRate) = 0;
    };
}
