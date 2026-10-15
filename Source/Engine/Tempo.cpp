#include "Tempo.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace atm
{

namespace
{
    // The onset-strength function runs at 100 Hz, the model's own 10 ms grid,
    // so note onsets land exactly on frames.
    constexpr int    kSampleRate = 16000;
    constexpr int    kHop        = 160;                 // 10 ms
    constexpr int    kFftOrder   = 9;                   // 512-point: 32 ms window
    constexpr int    kFrame      = 1 << kFftOrder;
    constexpr double kFrameRate  = (double) kSampleRate / kHop;

    constexpr double kMinBpm = 40.0, kMaxBpm = 240.0;
    constexpr double kPriorCentreBpm = 110.0, kPriorWidthOctaves = 1.0;

    // Spectral flux: half-wave-rectified rise in log magnitude per bin, summed.
    std::vector<float> onsetStrength (std::span<const float> samples, const std::function<bool()>& shouldAbort)
    {
        const int numFrames = (int) samples.size() / kHop;
        std::vector<float> odf ((size_t) juce::jmax (0, numFrames), 0.0f);
        if (numFrames <= 0)
            return odf;

        float peak = 1.0e-6f;
        for (const float s : samples)
            peak = juce::jmax (peak, std::abs (s));
        const float norm = 1.0f / peak;

        juce::dsp::FFT fft (kFftOrder);
        juce::dsp::WindowingFunction<float> window ((size_t) kFrame, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> buffer ((size_t) kFrame * 2), prevLog ((size_t) kFrame / 2 + 1, 0.0f), logMag (prevLog.size());

        for (int f = 0; f < numFrames; ++f)
        {
            if ((f & 255) == 0 && shouldAbort && shouldAbort())
                return {};

            // Frame centred on its hop position.
            const int start = f * kHop - kFrame / 2;
            for (int i = 0; i < kFrame; ++i)
            {
                const int s = start + i;
                buffer[(size_t) i] = (s >= 0 && s < (int) samples.size()) ? samples[(size_t) s] * norm : 0.0f;
            }
            std::fill (buffer.begin() + kFrame, buffer.end(), 0.0f);

            window.multiplyWithWindowingTable (buffer.data(), (size_t) kFrame);
            fft.performFrequencyOnlyForwardTransform (buffer.data(), true);

            float flux = 0.0f;
            for (size_t k = 0; k < logMag.size(); ++k)
            {
                logMag[k] = std::log1p (100.0f * buffer[k]);
                flux += juce::jmax (0.0f, logMag[k] - prevLog[k]);
            }
            odf[(size_t) f] = flux;
            std::swap (logMag, prevLog);
        }

        // Remove the slow trend (a 0.4 s running mean) so sustained loudness
        // doesn't read as rhythm, then keep only the rises.
        const int half = (int) (0.2 * kFrameRate);
        std::vector<float> trend (odf.size());
        double sum = 0.0;
        int count = 0;
        for (int f = 0; f < numFrames; ++f)
        {
            if (f + half < numFrames) { sum += odf[(size_t) (f + half)]; ++count; }
            if (f - half - 1 >= 0)    { sum -= odf[(size_t) (f - half - 1)]; --count; }
            trend[(size_t) f] = (float) (sum / juce::jmax (1, count));
        }
        for (int f = 0; f < numFrames; ++f)
            odf[(size_t) f] = juce::jmax (0.0f, odf[(size_t) f] - trend[(size_t) f]);

        return odf;
    }

    // Note onsets as a second onset function on the same frames. Drums and
    // bass carry more weight: they are what people tap along to.
    std::vector<float> noteStrength (std::span<const msl::Note> notes, size_t numFrames)
    {
        std::vector<float> ns (numFrames, 0.0f);
        for (const auto& note : notes)
        {
            const int f = juce::roundToInt (note.onset * kFrameRate);
            if (f < 0 || (size_t) f >= numFrames)
                continue;

            const bool bass = ! note.is_drum && note.program >= 32 && note.program <= 39;
            const float w = note.is_drum ? 1.5f : bass ? 1.25f : 1.0f;
            ns[(size_t) f] += w;
        }

        // A little width so a 10 ms miss still counts.
        std::vector<float> smooth (numFrames, 0.0f);
        for (size_t f = 0; f < numFrames; ++f)
        {
            smooth[f] += ns[f];
            if (f > 0)             smooth[f] += 0.5f * ns[f - 1];
            if (f + 1 < numFrames) smooth[f] += 0.5f * ns[f + 1];
        }
        return smooth;
    }

    void normaliseToUnitMean (std::vector<float>& v)
    {
        const double mean = v.empty() ? 0.0 : std::accumulate (v.begin(), v.end(), 0.0) / (double) v.size();
        if (mean > 1.0e-9)
            for (auto& x : v)
                x = (float) (x / mean);
    }

    float sampleAt (const std::vector<float>& v, double frame)
    {
        if (frame < 0.0 || frame >= (double) (v.size() - 1))
            return 0.0f;
        const int i = (int) frame;
        const float t = (float) (frame - i);
        return v[(size_t) i] * (1.0f - t) + v[(size_t) i + 1] * t;
    }

    double priorWeight (double bpm)
    {
        const double x = std::log2 (bpm / kPriorCentreBpm) / kPriorWidthOctaves;
        return std::exp (-0.5 * x * x);
    }

    /** Sum of the onset function at every beat of a grid, best phase in [0, period). */
    double gridFit (const std::vector<float>& odf, double periodFrames, double& bestPhaseFrames)
    {
        const int numFrames = (int) odf.size();
        double best = -1.0;
        bestPhaseFrames = 0.0;

        for (double phase = 0.0; phase < periodFrames; phase += 0.5)
        {
            double score = 0.0;
            for (double f = phase; f < numFrames; f += periodFrames)
                score += sampleAt (odf, f);

            if (score > best)
            {
                best = score;
                bestPhaseFrames = phase;
            }
        }
        return best;
    }
}

std::optional<NamedTempo> tempoFromFileName (const juce::String& fileName)
{
    const auto name = juce::File (fileName).getFileNameWithoutExtension().toLowerCase();
    constexpr double kMin = 50.0, kMax = 220.0;

    // Tokens of digits (with an optional decimal part), noting whether
    // "bpm" sits right next to them.
    std::optional<NamedTempo> bare;
    for (int i = 0; i < name.length();)
    {
        if (! juce::CharacterFunctions::isDigit (name[i]))
        {
            ++i;
            continue;
        }

        int end = i;
        while (end < name.length() && (juce::CharacterFunctions::isDigit (name[end]) || name[end] == '.'))
            ++end;

        const auto token = name.substring (i, end);
        const double value = token.getDoubleValue();
        const auto before = name.substring (0, i).trimEnd();
        const auto after = name.substring (end).trimStart();
        const bool labelled = after.startsWith ("bpm") || after.startsWith ("_bpm") || after.startsWith ("-bpm")
                           || before.endsWith ("bpm") || before.endsWith ("bpm_") || before.endsWith ("bpm-");

        // A bare number has to stand alone (between separators or the ends),
        // so "44k", "v2", "track01" and dates don't count.
        const auto isSeparator = [] (juce::juce_wchar c) { return c == '_' || c == '-' || c == ' ' || c == '.' || c == '(' || c == ')' || c == '['; };
        const bool standsAlone = (i == 0 || isSeparator (name[i - 1])) && (end == name.length() || isSeparator (name[end]));

        if (value >= kMin && value <= kMax)
        {
            if (labelled)
                return NamedTempo { value, true };
            if (standsAlone && ! bare.has_value())
                bare = NamedTempo { value, false };
        }

        i = end;
    }

    return bare;
}

TempoEstimate estimateTempo (std::span<const float> samples16k, std::span<const msl::Note> notes,
                             const std::function<bool()>& shouldAbort, std::optional<NamedTempo> named)
{
    TempoEstimate result;

    auto odf = onsetStrength (samples16k, shouldAbort);
    if (odf.size() < (size_t) (2.0 * kFrameRate))      // under two seconds: nothing to measure
        return result;

    // 1. Combine audio and note onsets, each at unit mean so neither dominates.
    normaliseToUnitMean (odf);
    if (! notes.empty())
    {
        auto ns = noteStrength (notes, odf.size());
        normaliseToUnitMean (ns);
        for (size_t f = 0; f < odf.size(); ++f)
            odf[f] = odf[f] + 0.75f * ns[f];
    }

    // 2. Coarse tempo: autocorrelation over the lag range, weighted by the prior.
    const int numFrames = (int) odf.size();
    const double mean = std::accumulate (odf.begin(), odf.end(), 0.0) / numFrames;
    std::vector<float> centred (odf.size());
    for (size_t f = 0; f < odf.size(); ++f)
        centred[f] = (float) (odf[f] - mean);

    const int minLag = (int) std::floor (60.0 * kFrameRate / kMaxBpm);
    const int maxLag = juce::jmin (numFrames / 2, (int) std::ceil (60.0 * kFrameRate / kMinBpm));
    if (maxLag <= minLag)
        return result;

    std::vector<double> acf ((size_t) maxLag + 1, 0.0);
    double acf0 = 0.0;
    for (const float x : centred)
        acf0 += (double) x * x;
    if (acf0 <= 0.0)
        return result;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (shouldAbort && shouldAbort())
            return result;

        double sum = 0.0;
        for (int f = lag; f < numFrames; ++f)
            sum += (double) centred[(size_t) f] * centred[(size_t) (f - lag)];
        acf[(size_t) lag] = sum / acf0;
    }

    int bestLag = minLag;
    double bestScore = -1.0, scoreSum = 0.0;   // scoreSum kept for a future "how peaked" diagnostic
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        // A beat period is supported by its own lag and by its multiples;
        // adding the double lag favours the pulse over its subdivision.
        double score = acf[(size_t) lag];
        if (2 * lag <= maxLag)
            score += 0.5 * acf[(size_t) (2 * lag)];
        score *= priorWeight (60.0 * kFrameRate / lag);
        scoreSum += juce::jmax (0.0, score);

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    // 3. Fine tempo and phase: fit a steady grid to the whole file around the
    //    coarse answer. A 10 ms lag is ~1.5% at 90 BPM, far too coarse for a
    //    three-minute file; the grid fit resolves to 0.05 BPM.
    const double coarseBpm = 60.0 * kFrameRate / bestLag;
    double bestBpm = coarseBpm, bestPhase = 0.0, bestFit = -1.0;

    for (double bpm = coarseBpm * 0.96; bpm <= coarseBpm * 1.04; bpm += 0.05)
    {
        if (shouldAbort && shouldAbort())
            return result;

        double phase = 0.0;
        const double fit = gridFit (odf, 60.0 * kFrameRate / bpm, phase);
        if (fit > bestFit)
        {
            bestFit = fit;
            bestBpm = bpm;
            bestPhase = phase;
        }
    }

    // A tempo from the file name, when the audio backs it (its own pulse a
    // simple multiple or fraction of the name's) or the name says "bpm":
    // take it exactly, and fit where its beats fall.
    if (named.has_value())
    {
        bool agrees = named->labelled;
        for (const double k : { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 })
            agrees = agrees || std::abs (named->bpm / (bestBpm * k) - 1.0) < 0.06;

        if (agrees)
        {
            bestBpm = named->bpm;
            bestFit = gridFit (odf, 60.0 * kFrameRate / bestBpm, bestPhase);
        }
    }

    result.bpm = std::round (bestBpm * 100.0) / 100.0;
    result.firstBeat = bestPhase / kFrameRate;

    // How much louder the onsets on the grid are than the average frame: 1
    // means the beats are no better than anywhere else (no rhythm found),
    // a clean click track scores in the tens.
    {
        const double beats = std::ceil ((numFrames - bestPhase) / (60.0 * kFrameRate / bestBpm));
        const double onGrid = bestFit / juce::jmax (1.0, beats);
        const double overall = std::accumulate (odf.begin(), odf.end(), 0.0) / numFrames;
        result.confidence = (float) juce::jlimit (0.0, 1.0, (onGrid / juce::jmax (1.0e-9, overall) - 1.0) / 4.0);
    }
    juce::ignoreUnused (scoreSum);

    // 4. Downbeat: the beat position (mod 4) where low-end onsets -- kick,
    //    bass, and the audio's own accents -- pile up most.
    std::vector<float> low = odf;
    for (const auto& note : notes)
    {
        const int f = juce::roundToInt (note.onset * kFrameRate);
        if (f < 0 || f >= numFrames)
            continue;
        const bool kick = note.is_drum && (note.pitch == 35 || note.pitch == 36);
        const bool bass = ! note.is_drum && note.program >= 32 && note.program <= 39;
        if (kick || bass)
            low[(size_t) f] += 2.0f;
    }

    const double period = 60.0 * kFrameRate / result.bpm;
    double bestBar = -1.0;
    for (int d = 0; d < result.beatsPerBar; ++d)
    {
        double score = 0.0;
        for (double f = bestPhase + d * period; f < numFrames; f += result.beatsPerBar * period)
            score += sampleAt (low, f);

        if (score > bestBar)
        {
            bestBar = score;
            result.downbeat = d;
        }
    }

    // A beat a hair before the audio starts (within the export's grace) is
    // the audio's first beat: a loop cut on its downbeat, played a touch
    // late, would otherwise have its first bar counted as a pickup and the
    // ruler's "1" land on its second bar.
    {
        constexpr double kGraceBeats = 0.08;
        const double beatSeconds = 60.0 / result.bpm;
        if (beatSeconds - result.firstBeat <= kGraceBeats * beatSeconds)
        {
            result.firstBeat -= beatSeconds;
            result.downbeat = (result.downbeat + 1) % result.beatsPerBar;
        }
    }

    return result;
}

TempoEstimate gridFor (const HostTiming& host) noexcept
{
    TempoEstimate grid;
    grid.bpm = host.bpm > 0.0 ? host.bpm : 120.0;
    grid.beatsPerBar = juce::jmax (1, (int) std::lround (host.quartersPerBar));
    grid.confidence = 1.0f;

    // The first whole quarter note at or after the first sample.
    const double firstBeatPpq = std::ceil (host.ppqAtStart - 1.0e-6);
    grid.firstBeat = (firstBeatPpq - host.ppqAtStart) * 60.0 / grid.bpm;

    // How far into its bar that beat is; the next bar line is the first
    // numbered bar, unless the beat is a bar line itself.
    const int offset = (((int) std::lround (firstBeatPpq - host.barStartPpq) % grid.beatsPerBar) + grid.beatsPerBar) % grid.beatsPerBar;
    grid.downbeat = (grid.beatsPerBar - offset) % grid.beatsPerBar;
    grid.firstBarNumber = host.startBar + (offset > 0 ? 1 : 0);
    return grid;
}

int leadInBeatsFor (const TempoEstimate& hostGrid) noexcept
{
    return (hostGrid.beatsPerBar - hostGrid.downbeat) % hostGrid.beatsPerBar;
}

} // namespace atm
