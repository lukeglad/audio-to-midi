#include "NoteEnds.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace atm
{

namespace
{
    constexpr double kSampleRate = 16000.0;
    constexpr int    kHop = 320;             // 20 ms
    constexpr int    kWindow = 640;          // 40 ms, Hann
    constexpr double kFloorDb = -20.0;       // still sounding: within this of the note's own peak
    constexpr double kAttackDb = 3.0;        // louder than the note ever was: someone else's event
    constexpr double kReferenceSeconds = 0.15; // the peak is read over at least this much of the note: a tone takes time to bloom
    constexpr double kChordSeconds = 0.05;   // onsets this close together are one chord, not a new event
    constexpr double kMaxHoldSeconds = 10.0;
    constexpr double kMinPeakDb = -60.0;     // quieter than this at its peak: leave the note alone
    constexpr double kMaxFrequency = 7000.0; // above the resampler's anti-alias cutoff there is nothing to measure

    /** Energy at `frequency` in the frame starting at `start` (Hann-windowed Goertzel). */
    double energyAt (std::span<const float> samples, int start, double frequency)
    {
        const int end = juce::jmin ((int) samples.size(), start + kWindow);
        if (end - start < kWindow / 2)
            return 0.0;

        const double coeff = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * frequency / kSampleRate);
        double s1 = 0.0, s2 = 0.0;

        for (int i = start; i < end; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i - start) / (kWindow - 1));
            const double s0 = w * samples[(size_t) i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (kWindow * kWindow * 0.25);
    }

    double toDb (double energy) { return 10.0 * std::log10 (energy + 1.0e-20); }
}

std::vector<msl::Note> holdNoteEnds (std::vector<msl::Note> notes, std::span<const float> samples,
                                     const std::function<bool()>& shouldAbort)
{
    const double length = (double) samples.size() / kSampleRate;
    if (samples.empty())
        return notes;

    // A hold never runs into the next note that starts on the same layer
    // after the model's end (a new chord, a melody note): it only ever
    // fills a stretch where nothing new begins there, which is what a
    // final chord ringing out is. Notes that start together are one chord.
    std::map<int, std::vector<double>> onsetsByLayer;
    for (const auto& n : notes)
        if (! n.is_drum)
            onsetsByLayer[n.program].push_back (n.onset);
    for (auto& [layer, onsets] : onsetsByLayer)
        std::sort (onsets.begin(), onsets.end());

    const auto nextOnLayer = [&] (const msl::Note& n)
    {
        const auto& onsets = onsetsByLayer[n.program];
        const auto it = std::lower_bound (onsets.begin(), onsets.end(), juce::jmax (n.offset, n.onset + kChordSeconds));
        return it != onsets.end() ? *it : length;
    };

    std::vector<double> modelOffsets (notes.size());
    for (size_t i = 0; i < notes.size(); ++i)
        modelOffsets[i] = notes[i].offset;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (shouldAbort && shouldAbort())
            return notes;

        auto& note = notes[i];
        if (note.is_drum)
            continue;

        const double frequency = 440.0 * std::pow (2.0, (note.pitch - 69) / 12.0);
        if (frequency > kMaxFrequency || note.offset <= note.onset)
            continue;

        const double limit = juce::jmin (length, juce::jmin (nextOnLayer (note), note.offset + kMaxHoldSeconds));
        if (limit <= note.offset)
            continue;

        // The note's own peak, over the span the model gave it or the first
        // 150 ms, whichever is longer: a 10 ms note has not bloomed yet.
        const int firstFrame = (int) (note.onset * kSampleRate) / kHop;
        const int referenceEnd = juce::jmax (firstFrame, (int) (juce::jmax (note.offset, note.onset + kReferenceSeconds) * kSampleRate) / kHop);
        double peak = 0.0;
        for (int f = firstFrame; f <= referenceEnd; ++f)
            peak = juce::jmax (peak, energyAt (samples, f * kHop, frequency));

        const double peakDb = toDb (peak);
        if (peakDb < kMinPeakDb)
            continue;

        // Walk on from the model's end while it still sounds: the end lands
        // at the first frame that has faded, got louder than the note ever
        // was, or run into the limit.
        const int modelEndFrame = juce::jmax (firstFrame, (int) (note.offset * kSampleRate) / kHop);
        double held = note.offset;

        for (int f = modelEndFrame + 1; (f * kHop) / kSampleRate < limit; ++f)
        {
            const double db = toDb (energyAt (samples, f * kHop, frequency));
            if (db < peakDb + kFloorDb || db > peakDb + kAttackDb)
                break;
            held = (f * kHop) / kSampleRate;
        }

        note.offset = juce::jmax (note.offset, juce::jmin (held, limit));
    }

    // A chord rings as a chord: notes that began and (per the model) ended
    // together on one layer take the longest hold among them, rather than
    // each string's partial fading out of the picture on its own.
    std::map<std::tuple<int, int, int>, double> chordEnd;
    const auto chordKey = [&] (size_t i)
    {
        const auto& n = notes[i];
        return std::make_tuple (n.program, (int) std::lround (n.onset / kChordSeconds),
                                (int) std::lround (modelOffsets[i] / kChordSeconds));
    };
    for (size_t i = 0; i < notes.size(); ++i)
        if (! notes[i].is_drum)
        {
            auto& end = chordEnd[chordKey (i)];
            end = juce::jmax (end, notes[i].offset);
        }
    for (size_t i = 0; i < notes.size(); ++i)
        if (! notes[i].is_drum)
            notes[i].offset = juce::jmax (notes[i].offset, chordEnd[chordKey (i)]);

    return notes;
}

} // namespace atm
