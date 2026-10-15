#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../SourceAudio.h"
#include "Cleanup.h"
#include "SimpleSynth.h"
#include "SoundFont.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace atm
{
    /**
        The preview: plays the original audio and the notes together, with a
        crossfade between them and per-layer mute and solo. Its own
        transport, independent of the host's.

        The notes play through a General MIDI soundfont (each layer on its
        instrument, drums on a kit) once one has been handed over, and
        through a simple synth until then or without one. The preview
        schedules; the synths only sound (see NoteSynth).

        Message-thread calls publish state; render() runs on the audio thread
        and never allocates, locks for long, or frees. New sources and notes
        arrive as immutable snapshots: the message thread keeps every snapshot
        alive for a while after replacing it, so the audio thread can read
        whichever one it last saw without owning it.
    */
    class Preview
    {
    public:
        static constexpr int kLayerSlots = 130;   // programs 0-127, drums at 128

        //======================================================================
        // Message thread.

        void setSource (std::shared_ptr<const SourceAudio>);
        void setNotes (const std::vector<MidiNote>&);

        /** The soundfont to play the notes through, from the next block on.
            Taken once: later calls are ignored. */
        void setSoundFont (std::shared_ptr<const SoundFont>);
        bool hasSoundFont() const noexcept { return fontSynthReady.load(); }

        void play();
        void stop();
        void togglePlay()                 { isPlaying() ? stop() : play(); }
        void seek (double seconds);

        bool isPlaying() const noexcept   { return playing.load(); }
        double getPosition() const noexcept;   // seconds
        double getLength() const noexcept { return length.load(); }

        /** 0 = original audio only, 1 = notes only. Equal-power crossfade. */
        void setMix (float);
        float getMix() const noexcept     { return mix.load(); }

        void setMuted (int layer, bool);
        void setSoloed (int layer, bool);
        bool isMuted (int layer) const noexcept;
        bool isSoloed (int layer) const noexcept;
        bool isAudible (int layer) const noexcept;

        //======================================================================
        // Audio thread.

        void prepare (double sampleRate, int maxBlockSize);

        /** Fills the buffer. @return false when there was nothing to play
            (the buffer is then silent), so a host that adds this on top of
            its own audio can skip the add. */
        bool render (juce::AudioBuffer<float>&);

    private:
        struct Snapshot
        {
            std::shared_ptr<const SourceAudio> source;
            std::vector<MidiNote> notes;   // sorted by onset
            float sourceGain = 1.0f;
            double lengthSeconds = 0.0;
        };

        // A note that has started and not yet ended: the scheduler's
        // bookkeeping, not a synth voice.
        struct Held
        {
            bool active = false;
            int layer = 0, pitch = 60;
            bool drum = false;
            double releaseSample = 0.0;   // absolute, on the timeline
            juce::uint32 order = 0;
        };

        // A note starting or ending at a sample of the current block.
        struct Event
        {
            int offset = 0;
            juce::uint32 sequence = 0;
            bool on = false;
            int layer = 0, pitch = 60;
            bool drum = false;
            int velocity = 100;
        };

        static constexpr int kMaxHeld = 96;
        static constexpr int kMaxEvents = 1024;

        void publish (Snapshot);
        void beginDeclick (double fromSeconds);
        NoteSynth& activeSynth() noexcept;
        void clearHeld();
        void startNote (const MidiNote&, int offset);
        void endHeld (Held&, int offset);
        void emit (const Event&);

        // Declick: a jump in the timeline (seek, scrub, stop) crossfades the
        // audio over a few ms instead of stepping, and starting playback
        // fades it in. The notes side needs none of this: a synth lets its
        // voices ring out through their release instead.
        bool wasPlaying = false;
        double lastEndSeconds = 0.0;      // where the last rendered block ended
        double fadeOutFromSeconds = 0.0;  // the old position, still playing out
        int fadeOutRemaining = 0, fadeInRemaining = 0, fadeLength = 256;

        // Snapshot handoff. `pending` is what the message thread last
        // published; the audio thread takes it under a try-lock and keeps a
        // raw pointer. `retired` keeps old snapshots alive on the message
        // thread until the audio thread must have moved on.
        juce::SpinLock snapshotLock;
        std::shared_ptr<Snapshot> pending;
        std::deque<std::pair<juce::uint32, std::shared_ptr<Snapshot>>> retired;
        const Snapshot* current = nullptr;
        std::atomic<double> length { 0.0 };

        std::atomic<bool> playing { false };
        std::atomic<double> positionSeconds { 0.0 };

        // Where the current (or last) play began: stop returns here, and so
        // does reaching the end. A seek moves it too. 0 for a new file.
        std::atomic<double> playStartSeconds { 0.0 };
        std::atomic<int> seekGeneration { 0 };
        int seenSeekGeneration = 0;
        std::atomic<float> mix { 1.0f };   // all notes by default (user's call 2026-10-15)

        std::array<std::atomic<bool>, kLayerSlots> muted {}, soloed {};
        std::atomic<int> soloCount { 0 };

        std::atomic<double> sampleRate { 44100.0 };
        size_t nextNote = 0;
        std::array<Held, kMaxHeld> held;
        juce::uint32 holdCounter = 0;
        std::array<Event, kMaxEvents> events;
        int numEvents = 0;
        std::vector<float> synthLeft, synthRight;

        // The synths. The soundfont one is built on the message thread and
        // handed over through the flag; it then stays for good, so the audio
        // thread can keep a reference without a lock.
        SimpleSynth simpleSynth;
        std::unique_ptr<SoundFontSynth> fontSynth;
        std::atomic<bool> fontSynthReady { false };
        NoteSynth* lastSynth = nullptr;   // audio thread: what the previous block played through
    };
}
