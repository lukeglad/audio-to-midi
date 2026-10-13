#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../SourceAudio.h"
#include "Cleanup.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace atm
{
    /**
        The preview: plays the original audio and a simple synth rendering of
        the notes together, with a crossfade between them and per-layer mute
        and solo. Its own transport, independent of the host's.

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

        void play();
        void stop();
        void togglePlay()                 { isPlaying() ? stop() : play(); }
        void seek (double seconds);

        bool isPlaying() const noexcept   { return playing.load(); }
        double getPosition() const noexcept;   // seconds
        double getLength() const noexcept { return length.load(); }

        /** 0 = original audio only, 1 = synth only. Equal-power crossfade. */
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
        void render (juce::AudioBuffer<float>&);

    private:
        struct Snapshot
        {
            std::shared_ptr<const SourceAudio> source;
            std::vector<MidiNote> notes;   // sorted by onset
            float sourceGain = 1.0f;
            double lengthSeconds = 0.0;
        };

        struct Voice
        {
            bool active = false;
            int layer = 0, pitch = 60;
            bool drum = false;
            int drumKind = 0;
            double startSample = 0.0, releaseSample = 0.0;
            double phase = 0.0, freq = 440.0;
            double env = 0.0;
            bool released = false;
            juce::uint32 noise = 1;

            // Set by a jump (seek, stop): the voice fades out over the
            // declick time instead of being cut, wherever the timeline went.
            bool fading = false;
            float fadeGain = 1.0f;
        };

        void publish (Snapshot);
        void trigger (const MidiNote&, double atSample);
        float renderVoice (Voice&, double sampleRate);
        void killAllVoices();
        void beginDeclick (double fromSeconds);

        // Declick: a jump in the timeline (seek, scrub, stop) crossfades over
        // a few ms instead of stepping, and starting playback fades in.
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
        std::atomic<float> mix { 0.5f };

        std::array<std::atomic<bool>, kLayerSlots> muted {}, soloed {};
        std::atomic<int> soloCount { 0 };

        double sampleRate = 44100.0;
        size_t nextNote = 0;
        std::array<Voice, 48> voices;
        std::vector<float> synthBuffer;
    };
}
