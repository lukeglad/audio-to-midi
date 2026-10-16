#include "Recorder.h"

namespace atm
{

namespace
{
    constexpr double kFifoSeconds = 8.0;     // room for the drain to fall well behind
    constexpr int    kDrainPollMs = 20;
    constexpr double kTrimLeadSeconds = 0.05, kTrimTailSeconds = 0.2;

    // A playhead more than this far from where playing on would have put it
    // has jumped (a loop wrapping, a locate). Blocks advance a fraction of a
    // beat, so this only trips on a real jump.
    constexpr double kJumpBeats = 0.25;

    HostTiming timingFor (const HostPosition& host)
    {
        HostTiming t;
        t.bpm = host.bpm > 0.0 ? host.bpm : 120.0;
        t.ppqAtStart = host.ppq;
        t.quartersPerBar = juce::jmax (0.25, host.timeSigNumerator * 4.0 / juce::jmax (1, host.timeSigDenominator));
        t.barStartPpq = host.barStartPpq;
        t.startBar = (int) std::lround (t.barStartPpq / t.quartersPerBar) + 1;
        t.settleBar();   // in case the host's bar start doesn't match its position
        return t;
    }
}

Recorder::Recorder()
    : juce::Thread ("recorder")
{
    startThread (juce::Thread::Priority::normal);
}

Recorder::~Recorder()
{
    stopThread (2000);
}

//==============================================================================
void Recorder::setArmed (bool shouldArm)
{
    if (shouldArm)
    {
        auto expected = Phase::idle;
        phase.compare_exchange_strong (expected, Phase::armed);
    }
    else
    {
        auto expected = Phase::armed;
        if (! phase.compare_exchange_strong (expected, Phase::idle))
            stop();
    }
}

void Recorder::stop()
{
    endCapture();
}

double Recorder::getSeconds() const noexcept
{
    return (double) samplesIn.load() / sampleRate.load();
}

std::optional<HostTiming> Recorder::getHostTiming() const
{
    if (phase.load (std::memory_order_acquire) != Phase::recording || ! startHasHost)
        return std::nullopt;
    return startTiming;
}

std::vector<float> Recorder::getLivePeaks() const
{
    const juce::SpinLock::ScopedLockType lock (peaksLock);
    return livePeaks;
}

//==============================================================================
void Recorder::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate.store (newSampleRate);
    const int capacity = juce::jmax (maxBlockSize * 4, (int) (kFifoSeconds * newSampleRate));
    fifo.setTotalSize (capacity);
    ring.setSize (2, capacity);
    ring.clear();
}

void Recorder::beginCapture (const HostPosition& host)
{
    fifo.reset();
    samplesIn.store (0);
    samplesDropped.store (0);
    peakSoFar.store (0.0f);
    startHasHost = host.hasTransport;
    startTiming = timingFor (host);
    hasExpectedPpq = false;
    phase.store (Phase::recording, std::memory_order_release);
}

bool Recorder::endCapture()
{
    auto expected = Phase::recording;
    return phase.compare_exchange_strong (expected, Phase::finishing);
}

void Recorder::process (const juce::AudioBuffer<float>& input, const HostPosition& host)
{
    lastBlockMs.store (juce::Time::getMillisecondCounter());
    transportSeen.store (host.hasTransport);

    auto current = phase.load (std::memory_order_acquire);

    if (current == Phase::armed)
    {
        // With a transport, wait for play; without one, now is the moment.
        if (host.hasTransport && ! host.playing)
            return;
        beginCapture (host);
        current = Phase::recording;
    }

    if (current != Phase::recording)
        return;

    const int numSamples = input.getNumSamples();
    const bool full = samplesIn.load() + numSamples > (juce::int64) (kMaxSeconds * sampleRate.load());

    // The pass is over when the host stops, or when its playhead jumps: a
    // loop wrapping back to its start would otherwise lay a second pass
    // after the first. The block that jumped belongs to the next pass.
    const bool jumped = host.hasTransport && hasExpectedPpq && std::abs (host.ppq - expectedPpq) > kJumpBeats;

    if ((host.hasTransport && ! host.playing) || jumped || full)
    {
        endCapture();
        return;
    }

    expectedPpq = host.ppq + numSamples / sampleRate.load() * host.bpm / 60.0;
    hasExpectedPpq = host.hasTransport;

    if (input.getNumChannels() == 0 || numSamples <= 0)
        return;

    const auto scope = fifo.write (numSamples);
    const int written = scope.blockSize1 + scope.blockSize2;
    const float* inL = input.getReadPointer (0);
    const float* inR = input.getReadPointer (juce::jmin (1, input.getNumChannels() - 1));

    if (scope.blockSize1 > 0)
    {
        ring.copyFrom (0, scope.startIndex1, inL, scope.blockSize1);
        ring.copyFrom (1, scope.startIndex1, inR, scope.blockSize1);
    }
    if (scope.blockSize2 > 0)
    {
        ring.copyFrom (0, scope.startIndex2, inL + scope.blockSize1, scope.blockSize2);
        ring.copyFrom (1, scope.startIndex2, inR + scope.blockSize1, scope.blockSize2);
    }

    samplesIn.fetch_add (written);
    if (written < numSamples)
        samplesDropped.fetch_add (numSamples - written);
}

//==============================================================================
void Recorder::run()
{
    while (! threadShouldExit())
    {
        wait (kDrainPollMs);
        drain();

        if (phase.load (std::memory_order_acquire) == Phase::finishing)
        {
            // A block the audio thread was still writing when the phase
            // changed lands within a few ms: give it that, then take it.
            wait (50);
            drain();
            finish();
        }
    }
}

void Recorder::drain()
{
    const int bucketSize = juce::jmax (1, (int) (kPeakBucketSeconds * sampleRate.load()));

    while (fifo.getNumReady() > 0)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        float peak = peakSoFar.load();

        const auto take = [&] (int start, int count)
        {
            if (count <= 0)
                return;

            const float* l = ring.getReadPointer (0, start);
            const float* r = ring.getReadPointer (1, start);
            left.insert (left.end(), l, l + count);
            right.insert (right.end(), r, r + count);

            for (int i = 0; i < count; ++i)
            {
                const float a = juce::jmax (std::abs (l[i]), std::abs (r[i]));
                peak = juce::jmax (peak, a);
                bucketPeak = juce::jmax (bucketPeak, a);
                if (++bucketFill >= bucketSize)
                {
                    const juce::SpinLock::ScopedLockType lock (peaksLock);
                    livePeaks.push_back (bucketPeak);
                    bucketPeak = 0.0f;
                    bucketFill = 0;
                }
            }
        };

        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
        peakSoFar.store (peak);
    }
}

void Recorder::finish()
{
    Result result;
    result.sampleRate = sampleRate.load();
    result.dropped = samplesDropped.load() > 0;

    const int total = (int) left.size();
    int first = -1, last = -1;
    for (int i = 0; i < total; ++i)
        if (juce::jmax (std::abs (left[(size_t) i]), std::abs (right[(size_t) i])) > kSilenceThreshold)
        {
            if (first < 0) first = i;
            last = i;
        }

    if (first < 0)
    {
        result.silent = true;
    }
    else
    {
        const int start = juce::jmax (0, first - (int) (kTrimLeadSeconds * result.sampleRate));
        const int end = juce::jmin (total, last + 1 + (int) (kTrimTailSeconds * result.sampleRate));
        result.samples.setSize (2, end - start);
        result.samples.copyFrom (0, 0, left.data() + start, end - start);
        result.samples.copyFrom (1, 0, right.data() + start, end - start);

        // A mono source on one side of a stereo input (a mic in input 2 of an
        // interface, a mono synth on one channel) came back in one ear only.
        // When one side is at least 40 dB below the other, the take is that
        // one source: it's copied to both sides, as a DAW records a mono input.
        const float rmsL = result.samples.getRMSLevel (0, 0, end - start);
        const float rmsR = result.samples.getRMSLevel (1, 0, end - start);
        if (juce::jmin (rmsL, rmsR) < juce::jmax (rmsL, rmsR) * kOneSidedRatio)
        {
            const int from = rmsL >= rmsR ? 0 : 1;
            result.samples.copyFrom (1 - from, 0, result.samples, from, 0, end - start);
            result.oneSided = true;
        }

        if (startHasHost)
        {
            auto timing = startTiming;
            timing.advance (start / result.sampleRate);
            result.host = timing;
        }
    }

    if (result.oneSided)
        juce::Logger::writeToLog ("recorder: sound on one side only, copied to both");
    if (result.dropped)
        juce::Logger::writeToLog ("recorder: " + juce::String (samplesDropped.load()) + " samples were dropped");

    left.clear();
    right.clear();
    left.shrink_to_fit();
    right.shrink_to_fit();
    bucketPeak = 0.0f;
    bucketFill = 0;
    {
        const juce::SpinLock::ScopedLockType lock (peaksLock);
        livePeaks.clear();
    }

    phase.store (Phase::idle, std::memory_order_release);

    if (onFinished)
        onFinished (std::move (result));
}

} // namespace atm
