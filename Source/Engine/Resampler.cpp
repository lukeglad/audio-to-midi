#include "Resampler.h"

#include <juce_dsp/juce_dsp.h>

namespace atm
{

namespace
{
    constexpr int kBlock = 1 << 15;

    // Below the new Nyquist (8 kHz) with room for the filter's transition band.
    constexpr float kAntiAliasCutoffHz = 7400.0f;
    constexpr int   kAntiAliasOrder    = 8;
}

std::vector<float> resampleForModel (const SourceAudio& source, const std::function<bool()>& shouldAbort)
{
    const int numChannels = source.samples.getNumChannels();
    const int length      = source.samples.getNumSamples();
    const double rate     = source.sampleRate;

    // 1. Mono. Averaged, not summed: a downmix louder than the input would
    //    clip a hot master, and the model is sensitive to level.
    std::vector<float> mono ((size_t) length);
    const float channelGain = 1.0f / (float) juce::jmax (1, numChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = source.samples.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            mono[(size_t) i] += in[i] * channelGain;
    }

    if (juce::exactlyEqual (rate, kModelSampleRate))
        return mono;

    // 2. Anti-alias before downsampling. The windowed-sinc interpolator below
    //    is an interpolator, not a decimator: it does not band-limit on its own.
    if (rate > kModelSampleRate)
    {
        auto coefficients = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (
            kAntiAliasCutoffHz, rate, kAntiAliasOrder);

        std::vector<juce::dsp::IIR::Filter<float>> stages;
        for (auto* c : coefficients)
            stages.emplace_back (c);

        for (int start = 0; start < length; start += kBlock)
        {
            if (shouldAbort && shouldAbort())
                return {};

            const int end = juce::jmin (length, start + kBlock);
            for (auto& stage : stages)
                for (int i = start; i < end; ++i)
                    mono[(size_t) i] = stage.processSample (mono[(size_t) i]);
        }
    }

    // 3. Resample. The interpolator reads 100 input samples behind the output
    //    position, so the input is padded with that many zeros at the end and
    //    the first (100 / ratio) outputs, which precede the start, are dropped.
    const double ratio   = rate / kModelSampleRate;
    const int   latency  = (int) juce::WindowedSincInterpolator::getBaseLatency();
    const int   outLen   = (int) std::floor ((double) length / ratio);
    const int   skip     = juce::roundToInt ((double) latency / ratio);
    const int   totalOut = outLen + skip;

    mono.resize ((size_t) (length + latency + (int) std::ceil (ratio) + 1), 0.0f);

    std::vector<float> out ((size_t) totalOut);
    juce::WindowedSincInterpolator interpolator;
    const float* in = mono.data();
    int produced = 0;

    while (produced < totalOut)
    {
        if (shouldAbort && shouldAbort())
            return {};

        const int n = juce::jmin (kBlock, totalOut - produced);
        in += interpolator.process (ratio, in, out.data() + produced, n);
        produced += n;
    }

    out.erase (out.begin(), out.begin() + skip);
    return out;
}

} // namespace atm
