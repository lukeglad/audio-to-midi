#include "Preview.h"

#include <cmath>

namespace atm
{

namespace
{
    constexpr double kPi = juce::MathConstants<double>::pi;
    constexpr double kTailSeconds = 0.6;      // keep rolling past the end for releases
    constexpr float  kSynthLevel = 0.11f;     // per voice, before the envelope

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

    // Drum kinds, from the GM percussion note.
    enum DrumKind { kick, snare, hat, tom, cymbal, other };

    DrumKind drumKindFor (int pitch) noexcept
    {
        switch (pitch)
        {
            case 35: case 36:                                   return kick;
            case 37: case 38: case 39: case 40:                 return snare;
            case 42: case 44: case 46:                          return hat;
            case 41: case 43: case 45: case 47: case 48: case 50: return tom;
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return cymbal;
            default:                                            return other;
        }
    }

    float nextNoise (juce::uint32& state) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
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
    sampleRate = newSampleRate;
    synthBuffer.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
    killAllVoices();
    ++seekGeneration;
}

void Preview::killAllVoices()
{
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.fading = false;
        voice.fadeGain = 1.0f;
    }
}

void Preview::trigger (const MidiNote& note, double atSample)
{
    // Steal the oldest voice if none is free.
    Voice* chosen = nullptr;
    for (auto& voice : voices)
        if (! voice.active) { chosen = &voice; break; }
    if (chosen == nullptr)
    {
        chosen = &voices[0];
        for (auto& voice : voices)
            if (voice.startSample < chosen->startSample)
                chosen = &voice;
    }

    auto& v = *chosen;
    v.active = true;
    v.released = false;
    v.fading = false;      // a reused voice starts fresh, whatever it was doing
    v.fadeGain = 1.0f;
    v.layer = note.layer;
    v.pitch = note.pitch;
    v.drum = note.drum;
    v.drumKind = drumKindFor (note.pitch);
    v.startSample = atSample;
    v.releaseSample = note.drum ? atSample : atSample + (note.offset - note.onset) * sampleRate;
    v.phase = 0.0;
    v.env = 0.0;
    v.freq = 440.0 * std::pow (2.0, (note.pitch - 69) / 12.0);
    v.noise = ((juce::uint32) note.pitch * 2654435761u) | 1u;
}

float Preview::renderVoice (Voice& v, double rate)
{
    // Time since the hit, in seconds, for the drum shapes.
    const double t = v.phase;   // drums use `phase` as elapsed seconds

    if (v.drum)
    {
        float out = 0.0f;
        double amp = 0.0, tau = 0.1;

        switch (v.drumKind)
        {
            case kick:
            {
                const double f = 45.0 + 90.0 * std::exp (-t / 0.045);
                v.env += 2.0 * kPi * f / rate;               // env doubles as the sine phase
                out = (float) std::sin (v.env);
                tau = 0.14;
                break;
            }
            case snare:
                out = 0.7f * nextNoise (v.noise) + 0.3f * (float) std::sin (2.0 * kPi * 190.0 * t);
                tau = 0.08;
                break;
            case hat:
            {
                // Differentiated noise: bright, thin.
                const float n = nextNoise (v.noise);
                out = n - (float) v.env;
                v.env = n;
                tau = v.pitch == 46 ? 0.18 : v.pitch == 44 ? 0.05 : 0.035;
                break;
            }
            case tom:
                out = (float) std::sin (2.0 * kPi * (80.0 + 12.0 * (v.pitch - 41)) * t * (1.0 + 0.3 * std::exp (-t / 0.05)));
                tau = 0.16;
                break;
            case cymbal:
            {
                const float n = nextNoise (v.noise);
                out = 0.5f * (n - (float) v.env);   // differentiated, like the hats, but long
                v.env = n;
                tau = 0.5;
                break;
            }
            default:
                out = nextNoise (v.noise);
                tau = 0.07;
                break;
        }

        amp = std::exp (-t / tau);
        v.phase += 1.0 / rate;
        if (amp < 0.002)
            v.active = false;
        return (float) (amp * 0.9) * out;
    }

    // Pitched: three harmonics. A 3 ms linear attack, then a one-pole decay
    // to a sustain, then a one-pole release once the note ends (or its layer
    // is silenced). `phase` holds the elapsed seconds.
    const double attack = 0.003, decayTau = 0.18, sustain = 0.55, releaseTau = 0.05;

    if (v.released)
        v.env += (0.0 - v.env) * (1.0 - std::exp (-1.0 / (releaseTau * rate)));
    else if (t < attack)
        v.env = juce::jmin (1.0, v.env + 1.0 / (attack * rate));
    else
        v.env += (sustain - v.env) * (1.0 - std::exp (-1.0 / (decayTau * rate)));

    v.phase += 1.0 / rate;
    const double ph = 2.0 * kPi * v.freq * v.phase;
    const float wave = (float) (std::sin (ph) + 0.5 * std::sin (2.0 * ph) + 0.25 * std::sin (3.0 * ph)) * 0.6f;

    if (v.released && v.env < 0.001)
        v.active = false;

    return (float) v.env * wave;
}

void Preview::beginDeclick (double fromSeconds)
{
    fadeOutFromSeconds = fromSeconds;
    fadeOutRemaining = fadeLength;

    for (auto& voice : voices)
        if (voice.active && ! voice.fading)
        {
            voice.fading = true;
            voice.fadeGain = 1.0f;
        }
}

void Preview::render (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    // Pick up a newly published snapshot, if the message thread isn't mid-swap.
    if (snapshotLock.tryEnter())
    {
        if (pending != nullptr && pending.get() != current)
        {
            current = pending.get();
            killAllVoices();
            nextNote = 0;
            fadeOutRemaining = fadeInRemaining = 0;
            seenSeekGeneration = -1;   // re-find the scheduler position below
        }
        snapshotLock.exit();
    }

    if (current == nullptr || numSamples <= 0)
        return;

    const auto& snapshot = *current;
    const bool isPlaying = playing.load();
    fadeLength = juce::jmax (32, (int) (0.006 * sampleRate));   // ~6 ms

    // A jump (seek, scrub, stop, new notes): move the scheduler to the first
    // note at or after the new position. If sound was playing, it doesn't
    // stop dead: the old position and its voices fade out under the new one.
    if (seenSeekGeneration != seekGeneration.load())
    {
        seenSeekGeneration = seekGeneration.load();

        if (wasPlaying)
        {
            beginDeclick (lastEndSeconds);   // the old position fades out...
            fadeInRemaining = fadeLength;    // ...while the new one fades in
        }
        else
        {
            killAllVoices();
        }

        const double pos = positionSeconds.load();
        nextNote = (size_t) std::distance (snapshot.notes.begin(),
                                           std::lower_bound (snapshot.notes.begin(), snapshot.notes.end(), pos,
                                                             [] (const MidiNote& n, double p) { return n.onset < p; }));
    }

    if (isPlaying && ! wasPlaying)
        fadeInRemaining = fadeLength;   // starting mid-song: fade in, don't step in

    bool anyFading = false;
    for (const auto& voice : voices)
        anyFading = anyFading || (voice.active && voice.fading);

    if (! isPlaying && fadeOutRemaining <= 0 && ! anyFading)
    {
        wasPlaying = false;
        return;
    }

    const double startSeconds = positionSeconds.load();
    const double endSeconds = startSeconds + numSamples / sampleRate;
    const double blockStartSample = startSeconds * sampleRate;

    // Schedule note-ons that fall inside this block.
    if (isPlaying)
        while (nextNote < snapshot.notes.size() && snapshot.notes[nextNote].onset < endSeconds)
        {
            const auto& note = snapshot.notes[nextNote++];
            if (note.onset >= startSeconds - 0.001 && isAudible (note.layer))
                trigger (note, note.onset * sampleRate);
        }

    // Synth, mono, then mixed into every channel.
    if ((int) synthBuffer.size() < numSamples)
        return;   // prepare() wasn't called with a big enough block; stay silent rather than allocate

    std::fill (synthBuffer.begin(), synthBuffer.begin() + numSamples, 0.0f);
    const float fadeStep = 1.0f / (float) fadeLength;

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        // A fading voice belongs to the old timeline: it plays from the top
        // of the block and only ramps down.
        const int from = voice.fading ? 0 : juce::jlimit (0, numSamples, (int) std::ceil (voice.startSample - blockStartSample));
        const bool silenced = ! isAudible (voice.layer);

        for (int i = from; i < numSamples; ++i)
        {
            if (! voice.fading)
            {
                const double sampleIndex = blockStartSample + i;
                if (! voice.released && (silenced || sampleIndex >= voice.releaseSample))
                    voice.released = true;
            }

            float sample = kSynthLevel * renderVoice (voice, sampleRate);

            if (voice.fading)
            {
                sample *= voice.fadeGain;
                voice.fadeGain -= fadeStep;
                if (voice.fadeGain <= 0.0f)
                    voice.active = false;
            }

            synthBuffer[(size_t) i] += sample;
            if (! voice.active)
                break;
        }
    }

    // Original audio, linearly interpolated from the file's rate: the new
    // position (fading in after a start or jump), plus the old position
    // fading out after a jump.
    const float m = mix.load();
    const float audioGain = std::cos (m * (float) kPi * 0.5f) * snapshot.sourceGain;
    const float synthGain = std::sin (m * (float) kPi * 0.5f);

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
            const double ratio = snapshot.source->sampleRate / sampleRate;

            const auto readAt = [&] (double pos) -> float
            {
                const int index = (int) pos;
                if (index < 0 || index + 1 >= srcLength)
                    return 0.0f;
                const float frac = (float) (pos - index);
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
                for (int i = 0, left = fadeOutAtStart; i < numSamples && left > 0; ++i, --left, readPos += ratio)
                    out[i] += audioGain * (float) left * fadeStep * readAt (readPos);
            }
        }

        if (synthGain > 0.0f)
            for (int i = 0; i < numSamples; ++i)
                out[i] += synthGain * synthBuffer[(size_t) i];

        for (int i = 0; i < numSamples; ++i)
            out[i] = softLimit (out[i]);
    }

    // Advance the declick state once for all channels.
    if (fadeOutRemaining > 0)
    {
        const int used = juce::jmin (fadeOutRemaining, numSamples);
        fadeOutRemaining -= used;
        fadeOutFromSeconds += used / sampleRate;
    }
    if (isPlaying)
        fadeInRemaining = juce::jmax (0, fadeInRemaining - numSamples);

    wasPlaying = isPlaying;

    if (! isPlaying)
        return;

    positionSeconds.store (endSeconds);
    lastEndSeconds = endSeconds;

    if (endSeconds >= snapshot.lengthSeconds + kTailSeconds)
    {
        playing.store (false);
        positionSeconds.store (playStartSeconds.load());   // back to where this play began
        ++seekGeneration;
    }
}

} // namespace atm
