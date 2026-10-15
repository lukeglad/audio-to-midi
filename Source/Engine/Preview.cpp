#include "Preview.h"

#include <algorithm>
#include <cmath>

namespace atm
{

namespace
{
    constexpr double kPi = juce::MathConstants<double>::pi;
    constexpr double kTailSeconds = 0.6;      // keep rolling past the end for releases
    constexpr double kMinNoteSeconds = 0.01;  // a note-off never lands before its note-on

    // A soft knee above 0.7 so stacked chords round off instead of clipping.
    float softLimit (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.7f)
            return x;
        const float y = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f);
        return x < 0.0f ? -y : y;
    }
    constexpr juce::uint32 kRetireAfterMs = 2000;

    int slotFor (int layer) noexcept { return layer >= Preview::kLayerSlots ? Preview::kLayerSlots - 1 : juce::jmax (0, layer); }
}

//==============================================================================
void Preview::setSource (std::shared_ptr<const SourceAudio> source)
{
    Snapshot snapshot;
    snapshot.source = std::move (source);
    if (snapshot.source != nullptr)
    {
        // Memos are often quiet: normalise to just under full scale.
        const float peak = snapshot.source->waveform.peak;
        snapshot.sourceGain = peak > 1.0e-4f ? 0.9f / peak : 1.0f;
        snapshot.lengthSeconds = snapshot.source->getLengthSeconds();
    }

    // Keep the notes we already have unless the audio actually changed.
    if (const juce::SpinLock::ScopedLockType lock (snapshotLock); pending != nullptr && pending->source == snapshot.source)
        snapshot.notes = pending->notes;

    publish (std::move (snapshot));
    stop();
    seek (0.0);
}

void Preview::setNotes (const std::vector<MidiNote>& notes)
{
    Snapshot snapshot;
    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        if (pending != nullptr)
        {
            snapshot.source = pending->source;
            snapshot.sourceGain = pending->sourceGain;
            snapshot.lengthSeconds = pending->lengthSeconds;
        }
    }

    snapshot.notes = notes;
    std::stable_sort (snapshot.notes.begin(), snapshot.notes.end(),
                      [] (const MidiNote& a, const MidiNote& b) { return a.onset < b.onset; });

    publish (std::move (snapshot));
    ++seekGeneration;   // the scheduler re-finds its place in the new list
}

void Preview::setSoundFont (std::shared_ptr<const SoundFont> font)
{
    if (font == nullptr || fontSynthReady.load())
        return;

    fontSynth = std::make_unique<SoundFontSynth> (std::move (font));
    fontSynthReady.store (true, std::memory_order_release);
}

void Preview::publish (Snapshot snapshot)
{
    auto fresh = std::make_shared<Snapshot> (std::move (snapshot));
    length.store (fresh->lengthSeconds);

    const auto now = juce::Time::getMillisecondCounter();
    {
        const juce::SpinLock::ScopedLockType lock (snapshotLock);
        if (pending != nullptr)
            retired.emplace_back (now, pending);
        pending = fresh;
    }

    // The audio thread picks up `pending` within a block; anything it could
    // still be reading was superseded far more recently than this.
    while (! retired.empty() && now - retired.front().first > kRetireAfterMs)
        retired.pop_front();
}

void Preview::play()
{
    if (length.load() <= 0.0)
        return;

    if (positionSeconds.load() >= length.load())
        seek (0.0);

    playStartSeconds.store (positionSeconds.load());
    playing.store (true);
}

void Preview::stop()
{
    // Back to where this play began, like a DAW transport: stop means "again
    // from there", and clicking the waveform is how to start somewhere else.
    if (playing.exchange (false))
    {
        positionSeconds.store (playStartSeconds.load());
        ++seekGeneration;
    }
}

void Preview::seek (double seconds)
{
    const double clamped = juce::jlimit (0.0, juce::jmax (0.0, length.load()), seconds);
    positionSeconds.store (clamped);
    playStartSeconds.store (clamped);   // the next stop returns here
    ++seekGeneration;
}

double Preview::getPosition() const noexcept
{
    return positionSeconds.load();
}

void Preview::setMix (float newMix)
{
    mix.store (juce::jlimit (0.0f, 1.0f, newMix));
}

void Preview::setMuted (int layer, bool shouldMute)
{
    muted[(size_t) slotFor (layer)].store (shouldMute);
}

void Preview::setSoloed (int layer, bool shouldSolo)
{
    auto& flag = soloed[(size_t) slotFor (layer)];
    if (flag.exchange (shouldSolo) != shouldSolo)
        soloCount.fetch_add (shouldSolo ? 1 : -1);
}

bool Preview::isMuted (int layer) const noexcept  { return muted[(size_t) slotFor (layer)].load(); }
bool Preview::isSoloed (int layer) const noexcept { return soloed[(size_t) slotFor (layer)].load(); }

bool Preview::isAudible (int layer) const noexcept
{
    const auto slot = (size_t) slotFor (layer);
    return ! muted[slot].load() && (soloCount.load() <= 0 || soloed[slot].load());
}

//==============================================================================
void Preview::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate.store (newSampleRate);
    synthLeft.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    synthRight.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    activeSynth().cutAll();
    simpleSynth.cutAll();
    clearHeld();
    ++seekGeneration;
}

NoteSynth& Preview::activeSynth() noexcept
{
    if (fontSynthReady.load (std::memory_order_acquire))
        return *fontSynth;
    return simpleSynth;
}

void Preview::clearHeld()
{
    for (auto& h : held)
        h.active = false;
}

void Preview::emit (const Event& event)
{
    if (numEvents < kMaxEvents)
    {
        events[(size_t) numEvents] = event;
        events[(size_t) numEvents].sequence = (juce::uint32) numEvents;
        ++numEvents;
    }
}

void Preview::endHeld (Held& h, int offset)
{
    emit ({ offset, 0, false, h.layer, h.pitch, h.drum, 100 });
    h.active = false;
}

void Preview::startNote (const MidiNote& note, int offset)
{
    // The same pitch again on the same layer ends what was sounding, the
    // way a keyboard would; otherwise a free slot, or the oldest note's.
    Held* slot = nullptr;
    Held* oldest = nullptr;

    for (auto& h : held)
    {
        if (h.active && h.layer == note.layer && h.pitch == note.pitch)
            endHeld (h, offset);

        if (! h.active)
        {
            if (slot == nullptr)
                slot = &h;
        }
        else if (oldest == nullptr || h.order < oldest->order)
        {
            oldest = &h;
        }
    }

    if (slot == nullptr)
    {
        endHeld (*oldest, offset);
        slot = oldest;
    }

    slot->active = true;
    slot->layer = note.layer;
    slot->pitch = note.pitch;
    slot->drum = note.drum;
    slot->releaseSample = juce::jmax (note.offset, note.onset + kMinNoteSeconds) * sampleRate.load();
    slot->order = ++holdCounter;

    emit ({ offset, 0, true, note.layer, note.pitch, note.drum, note.velocity });
}

void Preview::beginDeclick (double fromSeconds)
{
    fadeOutFromSeconds = fromSeconds;
    fadeOutRemaining = fadeLength;
}

bool Preview::render (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    // Pick up a newly published snapshot, if the message thread isn't mid-swap.
    bool snapshotChanged = false;
    if (snapshotLock.tryEnter())
    {
        if (pending != nullptr && pending.get() != current)
        {
            current = pending.get();
            snapshotChanged = true;
        }
        snapshotLock.exit();
    }

    if (current == nullptr || numSamples <= 0)
        return false;

    if ((int) synthLeft.size() < numSamples)
        return false;   // prepare() wasn't called with a big enough block; stay silent rather than allocate

    const auto& snapshot = *current;
    const bool isPlaying = playing.load();
    const double rate = sampleRate.load();
    fadeLength = juce::jmax (32, (int) (0.006 * rate));   // ~6 ms

    // The soundfont arriving (once): the simple synth lets go of what it
    // was playing and rings out underneath the new one.
    auto& synth = activeSynth();
    if (&synth != lastSynth)
    {
        if (lastSynth != nullptr)
            lastSynth->releaseAll();
        lastSynth = &synth;
        clearHeld();
    }

    // A jump (seek, scrub, stop) or new notes: move the scheduler to the
    // first note at or after the position. If sound was playing, it doesn't
    // stop dead: the audio's old position fades out under the new one, and
    // the notes ring out through their releases.
    if (snapshotChanged || seenSeekGeneration != seekGeneration.load())
    {
        seenSeekGeneration = seekGeneration.load();

        if (wasPlaying)
        {
            beginDeclick (lastEndSeconds);
            fadeInRemaining = fadeLength;
            synth.releaseAll();
        }
        else
        {
            synth.cutAll();
            fadeOutRemaining = fadeInRemaining = 0;
        }

        clearHeld();

        const double pos = positionSeconds.load();
        nextNote = (size_t) std::distance (snapshot.notes.begin(),
                                           std::lower_bound (snapshot.notes.begin(), snapshot.notes.end(), pos,
                                                             [] (const MidiNote& n, double p) { return n.onset < p; }));
    }

    if (isPlaying && ! wasPlaying)
        fadeInRemaining = fadeLength;   // starting mid-song: fade in, don't step in

    if (! isPlaying && fadeOutRemaining <= 0 && synth.isSilent() && simpleSynth.isSilent())
    {
        wasPlaying = false;
        return false;
    }

    const double startSeconds = positionSeconds.load();
    const double endSeconds = startSeconds + numSamples / rate;
    const double blockStartSample = startSeconds * rate;

    // This block's events: notes ending (or silenced by mute / solo), then
    // notes starting. More than the array takes waits for the next block.
    numEvents = 0;

    for (auto& h : held)
    {
        if (! h.active)
            continue;

        const bool silenced = ! isAudible (h.layer);
        if (silenced || h.releaseSample < blockStartSample + numSamples)
            endHeld (h, silenced ? 0 : juce::jlimit (0, numSamples - 1, (int) std::ceil (h.releaseSample - blockStartSample)));
    }

    if (isPlaying)
        while (nextNote < snapshot.notes.size() && snapshot.notes[nextNote].onset < endSeconds
               && numEvents + 2 <= kMaxEvents)
        {
            const auto& note = snapshot.notes[nextNote++];
            if (isAudible (note.layer))
                startNote (note, juce::jlimit (0, numSamples - 1, (int) std::ceil (note.onset * rate - blockStartSample)));
        }

    std::sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b)
    {
        return a.offset < b.offset || (a.offset == b.offset && a.sequence < b.sequence);
    });

    // The notes, rendered in segments split at every event so each lands
    // on its sample.
    float* left = synthLeft.data();
    float* right = synthRight.data();
    std::fill_n (left, numSamples, 0.0f);
    std::fill_n (right, numSamples, 0.0f);

    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const auto& event = events[(size_t) e];
        if (event.offset > pos)
        {
            synth.render (left + pos, right + pos, event.offset - pos, rate);
            pos = event.offset;
        }

        if (event.on)
            synth.noteOn (event.layer, event.pitch, event.drum, event.velocity);
        else
            synth.noteOff (event.layer, event.pitch, event.drum);
    }
    if (pos < numSamples)
        synth.render (left + pos, right + pos, numSamples - pos, rate);

    if (&synth != &simpleSynth && ! simpleSynth.isSilent())
        simpleSynth.render (left, right, numSamples, rate);

    // Original audio, linearly interpolated from the file's rate: the new
    // position (fading in after a start or jump), plus the old position
    // fading out after a jump.
    const float m = mix.load();
    const float audioGain = std::cos (m * (float) kPi * 0.5f) * snapshot.sourceGain;
    const float synthGain = std::sin (m * (float) kPi * 0.5f);
    const float fadeStep = 1.0f / (float) fadeLength;

    const int fadeInAtStart = fadeInRemaining, fadeOutAtStart = fadeOutRemaining;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* out = buffer.getWritePointer (ch);

        if (snapshot.source != nullptr && audioGain > 0.0f)
        {
            const auto& src = snapshot.source->samples;
            const int srcChannel = juce::jmin (ch, src.getNumChannels() - 1);
            const float* in = src.getReadPointer (srcChannel);
            const int srcLength = src.getNumSamples();
            const double ratio = snapshot.source->sampleRate / rate;

            const auto readAt = [&] (double readPos) -> float
            {
                const int index = (int) readPos;
                if (index < 0 || index + 1 >= srcLength)
                    return 0.0f;
                const float frac = (float) (readPos - index);
                return in[index] + (in[index + 1] - in[index]) * frac;
            };

            if (isPlaying)
            {
                double readPos = startSeconds * snapshot.source->sampleRate;
                int fadeIn = fadeInAtStart;
                for (int i = 0; i < numSamples; ++i, readPos += ratio)
                {
                    float gain = audioGain;
                    if (fadeIn > 0)
                    {
                        gain *= 1.0f - (float) fadeIn * fadeStep;
                        --fadeIn;
                    }
                    out[i] += gain * readAt (readPos);
                }
            }

            if (fadeOutAtStart > 0)
            {
                double readPos = fadeOutFromSeconds * snapshot.source->sampleRate;
                for (int i = 0, remaining = fadeOutAtStart; i < numSamples && remaining > 0; ++i, --remaining, readPos += ratio)
                    out[i] += audioGain * (float) remaining * fadeStep * readAt (readPos);
            }
        }

        if (synthGain > 0.0f)
        {
            // Stereo to stereo; a mono output gets the two sides averaged.
            if (numChannels == 1)
                for (int i = 0; i < numSamples; ++i)
                    out[i] += synthGain * 0.5f * (left[i] + right[i]);
            else
                juce::FloatVectorOperations::addWithMultiply (out, ch == 0 ? left : right, synthGain, numSamples);
        }

        for (int i = 0; i < numSamples; ++i)
            out[i] = softLimit (out[i]);
    }

    // Advance the declick state once for all channels.
    if (fadeOutRemaining > 0)
    {
        const int used = juce::jmin (fadeOutRemaining, numSamples);
        fadeOutRemaining -= used;
        fadeOutFromSeconds += used / rate;
    }
    if (isPlaying)
        fadeInRemaining = juce::jmax (0, fadeInRemaining - numSamples);

    wasPlaying = isPlaying;

    if (! isPlaying)
        return true;

    positionSeconds.store (endSeconds);
    lastEndSeconds = endSeconds;

    if (endSeconds >= snapshot.lengthSeconds + kTailSeconds)
    {
        playing.store (false);
        positionSeconds.store (playStartSeconds.load());   // back to where this play began
        ++seekGeneration;
    }

    return true;
}

} // namespace atm
