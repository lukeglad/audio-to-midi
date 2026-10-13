#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "NoteSynth.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

struct tsf;

namespace atm
{
    /** ~/Library/Application Support/AudioFlower/soundfonts/GeneralUser-GS.sf2
        (shared by any AudioFlower plugin; see external/GeneralUser-GS). */
    juce::File soundFontFile();

    //==========================================================================
    /**
        A loaded General MIDI soundfont: the samples and presets, read-only
        and shared by every SoundFontSynth made from it. About 65 MB in
        memory for GeneralUser GS (its 16-bit samples held as floats).
    */
    class SoundFont
    {
    public:
        /** Reads the whole file; a few hundred ms. Null if it's missing or
            isn't a soundfont. Call off the audio thread. */
        static std::shared_ptr<const SoundFont> load (const juce::File&);

        ~SoundFont();

        /** The preset for a layer key: its GM program in bank 0, or the
            standard kit (bank 128) for drums. -1 if the font has nothing. */
        int presetFor (int layer) const noexcept;

        tsf* handle() const noexcept { return font; }
        const juce::String& getName() const noexcept { return name; }

    private:
        SoundFont (tsf*, juce::String name);

        tsf* font = nullptr;
        juce::String name;
        std::array<int, 130> presets {};

        JUCE_DECLARE_NON_COPYABLE (SoundFont)
    };

    //==========================================================================
    /**
        Loads the soundfont once per process, in the background, as soon as
        the first plugin instance exists (hold it through a
        juce::SharedResourcePointer). Broadcasts on the message thread when
        the load finishes, with or without a font.
    */
    class SoundFontLibrary : public juce::ChangeBroadcaster,
                             private juce::Thread
    {
    public:
        SoundFontLibrary();
        ~SoundFontLibrary() override;

        /** Null until loaded, and for good if the file is missing. Any thread. */
        std::shared_ptr<const SoundFont> get() const;

    private:
        void run() override;

        mutable juce::SpinLock lock;
        std::shared_ptr<const SoundFont> font;

        JUCE_DECLARE_WEAK_REFERENCEABLE (SoundFontLibrary)
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundFontLibrary)
    };

    //==========================================================================
    /**
        Plays notes through a SoundFont with TinySoundFont. Each preview has
        its own (its own voices); the font's samples are shared. Built on the
        message thread, which is where its voices are allocated; from then on
        everything happens on the audio thread without allocating.
    */
    class SoundFontSynth : public NoteSynth
    {
    public:
        static constexpr int kMaxVoices = 64;

        explicit SoundFontSynth (std::shared_ptr<const SoundFont>);
        ~SoundFontSynth() override;

        void noteOn (int layer, int pitch, bool drum) override;
        void noteOff (int layer, int pitch, bool drum) override;
        void releaseAll() override;
        void cutAll() override;
        bool isSilent() const override;
        void render (float* left, float* right, int numSamples, double sampleRate) override;

    private:
        std::shared_ptr<const SoundFont> font;   // keeps the samples alive
        tsf* player = nullptr;                   // this synth's voices over the shared samples
        double currentRate = 0.0;
        std::vector<float> scratch;              // unweaved stereo, fixed size: longer blocks render in pieces

        JUCE_DECLARE_NON_COPYABLE (SoundFontSynth)
    };
}
