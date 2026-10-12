#include "MidiExport.h"
#include "TranscriptionService.h"

#include <map>

namespace atm
{

juce::String layerName (int layerKey)
{
    return layerKey == kDrumLayer ? juce::String ("drums") : instrumentDisplayName (layerKey);
}

juce::String pitchName (int midiNote)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12 - 1);
}

int ExportTiming::leadInBeats() const noexcept
{
    if (! conform)
        return 0;

    // Enough whole beats that the earliest note (at beat beatAt(earliestOnset),
    // negative when it precedes beat 0) lands at or after tick 0, then more
    // until the downbeat sits on a bar line. A note a few ms before a beat is
    // that beat, not a pickup: it gets rounded onto it rather than costing a
    // whole bar of lead-in.
    constexpr double kGrace = 0.08;   // beats
    int lead = juce::jmax (0, (int) std::ceil (-tempo.beatAt (earliestOnset) - kGrace));
    while ((lead + tempo.downbeat) % tempo.beatsPerBar != 0)
        ++lead;
    return lead;
}

double earliestOnsetOf (const std::vector<MidiNote>& notes) noexcept
{
    double earliest = std::numeric_limits<double>::max();
    for (const auto& note : notes)
        earliest = juce::jmin (earliest, note.onset);
    return notes.empty() ? 0.0 : earliest;
}

double tickFor (double seconds, const ExportTiming& timing)
{
    if (timing.conform)
        return (timing.tempo.beatAt (seconds) + timing.leadInBeats()) * kTicksPerQuarter;

    return seconds * kTicksPerQuarter * juce::jlimit (20.0, 999.0, timing.hostBpm) / 60.0;
}

juce::MidiFile buildLayerMidi (const std::vector<MidiNote>& allNotes, int layerKey, const ExportTiming& timing)
{
    const double bpm = juce::jlimit (20.0, 999.0, timing.hostBpm);
    const bool drums = layerKey == kDrumLayer;
    const int channel = drums ? 10 : 1;

    // Per pitch, in onset order, so each note can be cut at the next one.
    struct Span { int start, end, velocity; bool operator< (const Span& o) const { return start < o.start; } };
    std::map<int, std::vector<Span>> byPitch;

    for (const auto& note : allNotes)
    {
        if (note.layer != layerKey)
            continue;

        const int start = juce::jmax (0, juce::roundToInt (tickFor (note.onset, timing)));
        const int end   = juce::jmax (start + kMinNoteTicks, juce::roundToInt (tickFor (note.offset, timing)));
        byPitch[note.pitch].push_back ({ start, end, note.velocity });
    }

    juce::MidiMessageSequence ons, offs;
    int lastTick = 0;

    for (auto& [pitch, spans] : byPitch)
    {
        std::sort (spans.begin(), spans.end());

        for (size_t i = 0; i < spans.size(); ++i)
        {
            const int start = spans[i].start;

            // Quantize can stack two notes of one pitch on
            // the same tick, which MIDI can't represent: keep the first.
            if (i > 0 && start == spans[i - 1].start)
                continue;

            int end = spans[i].end;
            if (i + 1 < spans.size())
                end = juce::jmin (end, spans[i + 1].start);
            end = juce::jmax (end, start + 1);

            ons.addEvent  (juce::MidiMessage::noteOn  (channel, pitch, (juce::uint8) juce::jlimit (1, 127, spans[i].velocity)), start);
            offs.addEvent (juce::MidiMessage::noteOff (channel, pitch), end);
            lastTick = juce::jmax (lastTick, end);
        }
    }

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, layerName (layerKey)), 0);   // track name: FL names the clip by it
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    if (! drums)
        track.addEvent (juce::MidiMessage::programChange (channel, juce::jlimit (0, 127, layerKey)), 0);

    // Note-offs first: addEvent places a later-added event after earlier ones
    // at the same tick, so a note ending exactly where the next begins on the
    // same pitch is released before it is struck again.
    track.addSequence (offs, 0);
    track.addSequence (ons, 0);
    track.addEvent (juce::MidiMessage::endOfTrack(), lastTick);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kTicksPerQuarter);
    file.addTrack (track);
    return file;
}

bool writeMidiFile (const juce::MidiFile& midi, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    return ! out.failedToOpen() && midi.writeTo (out, 1) && out.getStatus().wasOk();
}

juce::String layerMidiFileName (const juce::String& audioFileName, int layerKey)
{
    const auto base = juce::File::createLegalFileName (juce::File (audioFileName).getFileNameWithoutExtension());
    return juce::File::createLegalFileName ((base.isEmpty() ? juce::String ("transcription") : base)
                                            + " - " + layerName (layerKey)) + ".mid";
}

} // namespace atm
