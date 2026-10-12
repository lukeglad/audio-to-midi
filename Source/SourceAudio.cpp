#include "SourceAudio.h"

namespace
{
    // Whole songs must work; this only stops a stray podcast or DJ set from
    // eating gigabytes. 15 minutes of 48 kHz stereo float is ~350 MB.
    constexpr double kMaxSeconds = 15.0 * 60.0;

    // Enough resolution for a full-width waveform on a large window; the
    // canvas reduces further per pixel column.
    constexpr int kMaxWaveformBuckets = 8192;

    DecodeResult fail (const juce::String& error) { return { nullptr, error }; }

    WaveformSummary summarise (const juce::AudioBuffer<float>& samples)
    {
        WaveformSummary summary;

        const int length      = samples.getNumSamples();
        const int numChannels = samples.getNumChannels();
        const int numBuckets  = juce::jlimit (1, kMaxWaveformBuckets, length);
        summary.mins.assign ((size_t) numBuckets, 0.0f);
        summary.maxs.assign ((size_t) numBuckets, 0.0f);

        const float channelGain = 1.0f / (float) numChannels;

        for (int b = 0; b < numBuckets; ++b)
        {
            const int start = (int) ((int64_t) b       * length / numBuckets);
            const int end   = (int) ((int64_t) (b + 1) * length / numBuckets);

            float lo = 0.0f, hi = 0.0f;
            for (int i = start; i < end; ++i)
            {
                float mono = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    mono += samples.getSample (ch, i);
                mono *= channelGain;

                lo = juce::jmin (lo, mono);
                hi = juce::jmax (hi, mono);
            }

            summary.mins[(size_t) b] = lo;
            summary.maxs[(size_t) b] = hi;
            summary.peak = juce::jmax (summary.peak, -lo, hi);
        }

        return summary;
    }
}

bool isSupportedAudioFile (const juce::File& file)
{
    return file.existsAsFile()
        && file.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3;ogg;m4a;aac;caf");
}

juce::String supportedFormatsLabel()
{
    return juce::String (juce::CharPointer_UTF8 ("wav \xC2\xB7 aiff \xC2\xB7 flac \xC2\xB7 mp3 \xC2\xB7 ogg \xC2\xB7 m4a"));
}

DecodeResult decodeAudioFile (const juce::File& file, const std::function<bool()>& shouldAbort)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return fail ("can't read this file");

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return fail ("this file has no audio in it");

    if ((double) reader->lengthInSamples / reader->sampleRate > kMaxSeconds)
        return fail ("files up to 15 minutes only");

    auto audio = std::make_shared<SourceAudio>();
    audio->fileName   = file.getFileName();
    audio->sampleRate = reader->sampleRate;

    // Mono stays mono; anything wider keeps its first two channels.
    const int numChannels = reader->numChannels > 1 ? 2 : 1;
    const int length      = (int) reader->lengthInSamples;
    audio->samples.setSize (numChannels, length);

    constexpr int blockSize = 1 << 16;
    for (int pos = 0; pos < length; pos += blockSize)
    {
        if (shouldAbort())
            return fail ({});

        const int n = juce::jmin (blockSize, length - pos);
        if (! reader->read (&audio->samples, pos, n, pos, true, numChannels > 1))
            return fail ("couldn't decode this file");
    }

    if (shouldAbort())
        return fail ({});

    audio->waveform = summarise (audio->samples);
    return { std::move (audio), {} };
}
