#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "HostTiming.h"

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace atm
{
    /** What the host's playhead says at the start of a block. */
    struct HostPosition
    {
        bool hasTransport = false;    // a host with a timeline; false in the standalone
        bool playing = false;
        double ppq = 0.0;             // quarter notes
        double barStartPpq = 0.0;     // the last bar start at or before ppq
        double bpm = 120.0;
        int timeSigNumerator = 4, timeSigDenominator = 4;
    };

    /**
        Captures the plugin's input while the host plays.

        Arm it; the recording begins at the first block the host is playing
        (at once, where there is no transport) and ends when the host stops,
        when its playhead jumps (a loop going round, a click elsewhere on the
        timeline: one pass, not two laid end to end), when stop() is called,
        or at the length cap. The audio thread writes
        into a FIFO that this object's own thread drains into memory, so a
        recording never depends on the message thread keeping up. When it
        ends, leading and trailing silence are trimmed (the host timing moves
        with the trim) and `onFinished` gets the result on the recorder's
        thread.
    */
    class Recorder : private juce::Thread
    {
    public:
        enum class Phase { idle, armed, recording, finishing };

        struct Result
        {
            double sampleRate = 0.0;
            juce::AudioBuffer<float> samples;   // stereo, trimmed of leading and trailing silence
            std::optional<HostTiming> host;     // where it began, adjusted for the trim; none without a transport
            bool silent = false;                // nothing above the threshold: `samples` is empty
            bool dropped = false;               // the drain fell behind and samples were lost
            bool oneSided = false;              // sound on one side only: copied to both
        };

        static constexpr double kMaxSeconds = 15.0 * 60.0;
        static constexpr float kSilenceThreshold = 0.00316f;   // -50 dBFS
        static constexpr float kOneSidedRatio = 0.01f;         // -40 dB: the other side is empty
        static constexpr double kPeakBucketSeconds = 0.02;

        Recorder();
        ~Recorder() override;

        //======================================================================
        // Message thread.

        /** Arms (idle -> armed); false disarms, or ends a recording in progress. */
        void setArmed (bool);

        /** Ends a recording in progress; the result follows on the recorder's thread. */
        void stop();

        Phase getPhase() const noexcept { return phase.load (std::memory_order_acquire); }

        /** Whether the last block came from a host with a timeline. */
        bool hasTransport() const noexcept { return transportSeen.load(); }

        /** Seconds captured so far in the current recording. */
        double getSeconds() const noexcept;

        /** The loudest sample so far: still ~0 after a couple of seconds means nothing is reaching the plugin. */
        float getPeakSoFar() const noexcept { return peakSoFar.load(); }

        /** The host position the current recording began at, while recording. */
        std::optional<HostTiming> getHostTiming() const;

        /** One peak per kPeakBucketSeconds of the current recording, for drawing it live. */
        std::vector<float> getLivePeaks() const;

        /** When the audio thread last ran, for noticing a host that stops calling. */
        juce::uint32 getLastBlockMs() const noexcept { return lastBlockMs.load(); }

        std::function<void (Result)> onFinished;

        //======================================================================
        // Audio thread.

        void prepare (double sampleRate, int maxBlockSize);
        void process (const juce::AudioBuffer<float>& input, const HostPosition&);

    private:
        void run() override;
        void drain();
        void finish();
        void beginCapture (const HostPosition&);
        bool endCapture();

        std::atomic<Phase> phase { Phase::idle };
        std::atomic<bool> transportSeen { false };
        std::atomic<juce::uint32> lastBlockMs { 0 };
        std::atomic<juce::int64> samplesIn { 0 };
        std::atomic<juce::int64> samplesDropped { 0 };
        std::atomic<float> peakSoFar { 0.0f };
        std::atomic<double> sampleRate { 48000.0 };

        // Written by the audio thread before it publishes Phase::recording,
        // read by others only while that phase holds.
        HostTiming startTiming;
        bool startHasHost = false;

        // Audio thread: where the host's playhead should be at the next
        // block if it simply played on.
        double expectedPpq = 0.0;
        bool hasExpectedPpq = false;

        juce::AbstractFifo fifo { 1 };
        juce::AudioBuffer<float> ring;

        // The drain thread's.
        std::vector<float> left, right;
        float bucketPeak = 0.0f;
        int bucketFill = 0;

        mutable juce::SpinLock peaksLock;
        std::vector<float> livePeaks;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
    };
}
