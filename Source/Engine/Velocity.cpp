#include "Velocity.h"

#include <cmath>
#include <map>

namespace atm
{

namespace
{
    constexpr double kPitchedWindow = 0.060;
    constexpr double kDrumWindow    = 0.030;
    constexpr float  kFloorDb       = -120.0f;

    /** Goertzel power at `frequency` over mono[start, start + length). */
    double goertzelPower (const std::vector<float>& mono, int start, int length, double frequency, double sampleRate)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        const double coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (int i = 0; i < length; ++i)
        {
            // Hann window, so a loud neighbour pitch leaks less into this one.
            const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / juce::jmax (1, length - 1));
            const double s0 = mono[(size_t) (start + i)] * hann + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / juce::jmax (1, length);
    }
}

std::vector<float> measureLevels (const SourceAudio& source, const std::vector<msl::Note>& notes,
                                  const std::function<bool()>& shouldAbort)
{
    const int length = source.samples.getNumSamples();
    const int channels = source.samples.getNumChannels();
    const double rate = source.sampleRate;

    std::vector<float> mono ((size_t) length, 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = source.samples.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += in[i] / (float) channels;
    }

    std::vector<float> levels (notes.size(), kFloorDb);

    for (size_t n = 0; n < notes.size(); ++n)
    {
        if ((n & 255) == 0 && shouldAbort && shouldAbort())
            return {};

        const auto& note = notes[n];
        const int start = juce::jlimit (0, length, (int) std::round (note.onset * rate));
        const int span  = juce::jmin (length - start, (int) std::round ((note.is_drum ? kDrumWindow : kPitchedWindow) * rate));
        if (span < 16)
            continue;

        double power = 0.0;

        if (note.is_drum)
        {
            for (int i = 0; i < span; ++i)
                power += (double) mono[(size_t) (start + i)] * mono[(size_t) (start + i)];
            power /= span;
        }
        else
        {
            const double f0 = 440.0 * std::pow (2.0, (note.pitch - 69) / 12.0);
            for (int harmonic = 1; harmonic <= 3; ++harmonic)
                if (f0 * harmonic < rate * 0.45)
                    power += goertzelPower (mono, start, span, f0 * harmonic, rate) / harmonic;
        }

        levels[n] = power > 0.0 ? (float) (10.0 * std::log10 (power)) : kFloorDb;
    }

    return levels;
}

namespace
{
    double percentile (std::vector<float> values, double p)
    {
        if (values.empty())
            return 0.0;
        const auto index = (size_t) juce::jlimit (0.0, (double) values.size() - 1.0, std::round (p * (double) (values.size() - 1)));
        std::nth_element (values.begin(), values.begin() + (std::ptrdiff_t) index, values.end());
        return values[index];
    }
}

std::vector<int> velocitiesFromLevels (const std::vector<msl::Note>& notes, const std::vector<float>& levels)
{
    std::vector<int> velocities (notes.size(), 100);
    if (levels.size() != notes.size() || notes.empty())
        return velocities;

    const auto layerOf = [] (const msl::Note& n) { return n.is_drum ? msl::DRUM_PROGRAM : n.program; };

    std::map<int, std::vector<float>> byLayer;
    for (size_t i = 0; i < notes.size(); ++i)
        byLayer[layerOf (notes[i])].push_back (levels[i]);

    std::map<int, std::pair<double, double>> span;   // layer -> (quiet dB, loud dB)
    for (auto& [layer, values] : byLayer)
        span[layer] = { percentile (values, 0.05), percentile (values, 0.95) };

    constexpr double kQuiet = 40.0, kLoud = 120.0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto [lo, hi] = span[layerOf (notes[i])];
        const double t = hi - lo > 0.5 ? (levels[i] - lo) / (hi - lo) : 0.5;
        velocities[i] = juce::jlimit (1, 127, juce::roundToInt (kQuiet + juce::jlimit (0.0, 1.0, t) * (kLoud - kQuiet)));
    }

    return velocities;
}

} // namespace atm
