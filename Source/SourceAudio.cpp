#include "SourceAudio.h"

namespace
{
    // Whole songs must work; this only stops a stray podcast or DJ set from
    // eating gigabytes. 15 minutes of 48 kHz stereo float is ~350 MB.
    constexpr double kMaxSeconds = 15.0 * 60.0;

    // Enough resolution for a full-width waveform on a large window; the
    // canvas reduces further per pixel column.
    constexpr int kMaxWaveformBuckets = 8192;

    DecodeResult fail (const juce::String& error) { return { nullptr, error, nullptr, {} }; }

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

namespace
{
    // Already-compressed containers are kept as they are; anything else gets
    // FLAC-encoded for the state.
    bool isCompressedContainer (const juce::String& extension)
    {
        return juce::StringArray { "mp3", "m4a", "aac", "ogg", "flac" }.contains (extension.toLowerCase());
    }

    DecodeResult decodeWithReader (std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& fileName,
                                   const std::function<bool()>& shouldAbort)
    {
        if (reader == nullptr)
            return fail ("can't read this file");

        if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return fail ("this file has no audio in it");

        if ((double) reader->lengthInSamples / reader->sampleRate > kMaxSeconds)
            return fail ("files up to 15 minutes only");

        auto audio = std::make_shared<SourceAudio>();
        audio->fileName   = fileName;
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
        return { std::move (audio), {}, nullptr, {} };
    }

    /** 16-bit FLAC of the decoded audio, for the state. Null on failure. */
    std::shared_ptr<const juce::MemoryBlock> encodeFlac (const SourceAudio& audio)
    {
        // The writer takes ownership of its stream and deletes it when done,
        // so the stream writes into a block we keep rather than its own.
        auto block = std::make_shared<juce::MemoryBlock>();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (*block, false);

        juce::FlacAudioFormat flac;
        auto writer = flac.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (audio.sampleRate)
                                                                                   .withNumChannels (audio.samples.getNumChannels())
                                                                                   .withBitsPerSample (16));
        if (writer == nullptr)
            return nullptr;

        if (! writer->writeFromAudioSampleBuffer (audio.samples, 0, audio.samples.getNumSamples()))
            return nullptr;

        writer.reset();   // flushes and trims the block to what was written
        return block->getSize() > 0 ? std::shared_ptr<const juce::MemoryBlock> (block) : nullptr;
    }
}

DecodeResult decodeAudioFile (const juce::File& file, const std::function<bool()>& shouldAbort)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto result = decodeWithReader (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)),
                                    file.getFileName(), shouldAbort);
    if (result.audio == nullptr)
        return result;

    const auto extension = file.getFileExtension().trimCharactersAtStart (".").toLowerCase();

    if (isCompressedContainer (extension))
    {
        auto bytes = std::make_shared<juce::MemoryBlock>();
        if (file.loadFileAsData (*bytes))
        {
            result.stateBytes = std::move (bytes);
            result.stateExtension = extension;
        }
    }
    else if (! shouldAbort())
    {
        result.stateBytes = encodeFlac (*result.audio);
        result.stateExtension = "flac";
    }

    return result;
}

DecodeResult decodeAudioData (std::shared_ptr<const juce::MemoryBlock> bytes, const juce::String& extension,
                              const juce::String& fileName, const std::function<bool()>& shouldAbort)
{
    if (bytes == nullptr || bytes->getSize() == 0)
        return fail ("the project holds no audio");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // The format is known, so ask it directly; the manager's guess is the
    // fallback for anything it can't place.
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (auto* format = formats.findFormatForFileExtension (extension))
        reader.reset (format->createReaderFor (new juce::MemoryInputStream (bytes->getData(), bytes->getSize(), false), true));
    if (reader == nullptr)
        reader.reset (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes->getData(), bytes->getSize(), false)));

    auto result = decodeWithReader (std::move (reader), fileName, shouldAbort);
    if (result.audio != nullptr)
    {
        result.stateBytes = std::move (bytes);
        result.stateExtension = extension;
    }
    return result;
}
