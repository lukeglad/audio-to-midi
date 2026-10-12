#include "Cleanup.h"

#include <cmath>

namespace atm
{

juce::String gridName (Grid grid)
{
    switch (grid)
    {
        case Grid::quarter:   return "1/4";
        case Grid::eighth:    return "1/8";
        case Grid::sixteenth: return "1/16";
    }
    return "1/16";
}

std::vector<MidiNote> cleanUp (const std::vector<msl::Note>& raw,
                               const CleanupSettings& settings,
                               const std::optional<TempoEstimate>& tempo)
{
    std::vector<MidiNote> out (raw.size());
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const auto& n = raw[i];
        out[i] = { n.onset, n.offset, n.pitch, layerKeyFor (n), n.is_drum, kDefaultVelocity };
    }

    // Quantize note starts toward the grid, keeping lengths.
    if (settings.quantize > 0.0 && tempo.has_value() && tempo->bpm > 0.0)
    {
        const double step = tempo->beatSeconds() / (double) (int) settings.grid;
        for (auto& note : out)
        {
            const double nearest = tempo->firstBeat + std::round ((note.onset - tempo->firstBeat) / step) * step;
            const double shift = (nearest - note.onset) * juce::jlimit (0.0, 1.0, settings.quantize);
            note.onset += shift;
            note.offset += shift;
        }
    }

    for (auto& note : out)
    {
        note.onset = juce::jmax (0.0, note.onset);
        note.offset = juce::jmax (note.onset + 0.005, note.offset);
    }

    return out;
}

} // namespace atm
