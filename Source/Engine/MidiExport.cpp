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

juce::MidiFile buildLayerMidi (const std::vector<msl::Note>& allNotes, int layerKey, double bpm)
{
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double ticksPerSecond = kTicksPerQuarter * bpm / 60.0;
    const bool drums = layerKey == kDrumLayer;
    const int channel = drums ? 10 : 1;

    // Per pitch, in onset order, so each note can be cut at the next one.
    std::map<int, std::vector<std::pair<int, int>>> byPitch;   // pitch -> (startTick, endTick)

    for (const auto& note : allNotes)
    {
        if (layerKeyFor (note) != layerKey)
            continue;

        const int start = juce::roundToInt (note.onset * ticksPerSecond);
        const int end   = juce::jmax (start + kMinNoteTicks, juce::roundToInt (note.offset * ticksPerSecond));
        byPitch[note.pitch].emplace_back (start, end);
    }

    juce::MidiMessageSequence ons, offs;
    int lastTick = 0;

    for (auto& [pitch, spans] : byPitch)
    {
        std::sort (spans.begin(), spans.end());

        for (size_t i = 0; i < spans.size(); ++i)
        {
            const int start = spans[i].first;

            if (i > 0 && start == spans[i - 1].first)
                continue;   // a duplicate onset on the same pitch can't be represented

            int end = spans[i].second;
            if (i + 1 < spans.size())
                end = juce::jmin (end, spans[i + 1].first);
            end = juce::jmax (end, start + 1);

            ons.addEvent  (juce::MidiMessage::noteOn  (channel, pitch, kFixedVelocity), start);
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
