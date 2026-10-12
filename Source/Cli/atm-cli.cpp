// atm-cli: decode -> resample -> transcribe from a terminal, through the same
// code the plugin uses.
//
//   atm-cli <audio file> [small|medium|large] [--cpu] [--notes N]
//
// Prints the device the model ran on, timings, a per-instrument count and
// the first N notes (default 20). Exit code 0 on success, 1 on failure, 2 on
// usage. The model must already be installed (the plugin downloads it).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "Engine/ModelFiles.h"
#include "Engine/Resampler.h"
#include "Engine/TranscriptionService.h"

#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>

namespace
{
    void print (const juce::String& line) { std::printf ("%s\n", line.toRawUTF8()); std::fflush (stdout); }

    int usage()
    {
        print ("usage: atm-cli <audio file> [small|medium|large] [--cpu] [--notes N]");
        print ("       atm-cli --resample-check     (alignment test, no model needed)");
        return 2;
    }

    /** A click at exactly 1.000 s, resampled from several rates: it must land
        at 16000 each time, or every onset would be shifted. Tolerance is 3
        samples (0.2 ms): the anti-alias filter's group delay adds ~2 when
        downsampling, which is 1/80th of the model's 10 ms grid. */
    int resampleCheck()
    {
        bool ok = true;

        for (const double rate : { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0 })
        {
            SourceAudio source;
            source.sampleRate = rate;
            source.samples.setSize (2, (int) (3.0 * rate));
            source.samples.clear();
            source.samples.setSample (0, (int) rate, 1.0f);
            source.samples.setSample (1, (int) rate, 1.0f);

            const auto out = atm::resampleForModel (source);
            const auto peak = std::max_element (out.begin(), out.end(),
                                                [] (float a, float b) { return std::abs (a) < std::abs (b); });
            const int index = (int) std::distance (out.begin(), peak);
            const int expected = (int) atm::kModelSampleRate;
            const bool pass = std::abs (index - expected) <= 3 && out.size() == (size_t) (3 * expected);
            ok = ok && pass;

            print (juce::String::formatted ("%6.0f Hz -> click at sample %d (expected %d), %d samples out: %s",
                                            rate, index, expected, (int) out.size(), pass ? "ok" : "WRONG"));
        }

        return ok ? 0 : 1;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager, for the service's WeakReferences

    if (argc == 2 && juce::String (argv[1]) == "--resample-check")
        return resampleCheck();

    juce::File file;
    auto model = atm::kDefaultModelSize;
    bool forceCpu = false;
    int notesToPrint = 20;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--cpu")
            forceCpu = true;
        else if (arg == "--notes" && i + 1 < argc)
            notesToPrint = juce::String (argv[++i]).getIntValue();
        else if (const auto size = atm::modelSizeFromName (arg))
            model = *size;
        else if (file == juce::File())
            file = juce::File::getCurrentWorkingDirectory().getChildFile (arg);
        else
            return usage();
    }

    if (file == juce::File())
        return usage();

    if (! atm::isModelInstalled (model))
    {
        print ("model '" + juce::String (atm::modelInfo (model).name) + "' is not installed in "
               + atm::modelsDirectory().getFullPathName());
        return 1;
    }

    // 1. Decode.
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto decoded = decodeAudioFile (file, [] { return false; });
    if (decoded.audio == nullptr)
    {
        print ("decode failed: " + decoded.error);
        return 1;
    }

    const auto& audio = *decoded.audio;
    print (juce::String::formatted ("file:     %s (%.1f s, %d ch, %.0f Hz)", audio.fileName.toRawUTF8(),
                                    audio.getLengthSeconds(), audio.samples.getNumChannels(), audio.sampleRate));
    print (juce::String::formatted ("decode:   %.0f ms", juce::Time::getMillisecondCounterHiRes() - t0));

    // 2. Transcribe, collecting what streams back.
    atm::TranscriptionService service;
    auto job = std::make_shared<atm::TranscriptionJob>();
    job->source = decoded.audio;
    job->model = model;
    job->forceCpu = forceCpu;

    std::vector<msl::Note> notes;
    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool done = false;
    atm::Outcome outcome = atm::Outcome::failed;
    juce::String error, device;
    double phaseStart = juce::Time::getMillisecondCounterHiRes();
    atm::Phase lastPhase = atm::Phase::queued;
    std::map<atm::Phase, double> phaseMs;

    job->onProgress = [&] (atm::Phase phase, float)
    {
        if (phase != lastPhase)
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            phaseMs[lastPhase] += now - phaseStart;
            phaseStart = now;
            lastPhase = phase;
        }
    };
    job->onNotes = [&] (std::vector<msl::Note> newNotes, double)
    {
        notes.insert (notes.end(), newNotes.begin(), newNotes.end());
    };
    job->onFinished = [&] (atm::Outcome o, juce::String e, juce::String d)
    {
        phaseMs[lastPhase] += juce::Time::getMillisecondCounterHiRes() - phaseStart;
        const std::lock_guard<std::mutex> lock (doneMutex);
        outcome = o; error = e; device = d; done = true;
        doneCv.notify_all();
    };

    service.submit (job);

    {
        std::unique_lock<std::mutex> lock (doneMutex);
        doneCv.wait (lock, [&] { return done; });
    }

    print (juce::String::formatted ("resample: %.0f ms", phaseMs[atm::Phase::resampling]));
    print (juce::String::formatted ("load:     %.0f ms", phaseMs[atm::Phase::loadingModel]));
    print (juce::String::formatted ("transcribe: %.0f ms (%.2fx real time)", phaseMs[atm::Phase::transcribing],
                                    audio.getLengthSeconds() * 1000.0 / juce::jmax (1.0, phaseMs[atm::Phase::transcribing])));
    print ("device:   " + device);

    if (outcome != atm::Outcome::finished)
    {
        print (outcome == atm::Outcome::cancelled ? "cancelled" : "failed: " + error);
        return 1;
    }

    // 3. Report.
    std::map<int, int> perProgram;
    for (const auto& note : notes)
        ++perProgram[note.program];

    print ("notes:    " + juce::String (notes.size()) + " in " + juce::String (perProgram.size()) + " instrument(s)");
    for (const auto& [program, count] : perProgram)
        print ("  " + atm::instrumentDisplayName (program).paddedRight (' ', 26) + juce::String (count));

    std::sort (notes.begin(), notes.end(), [] (const msl::Note& a, const msl::Note& b) { return a.onset < b.onset; });

    for (int i = 0; i < juce::jmin (notesToPrint, (int) notes.size()); ++i)
    {
        const auto& n = notes[(size_t) i];
        print (juce::String::formatted ("  %7.3f - %7.3f  pitch %3d  %s", n.onset, n.offset, n.pitch,
                                        atm::instrumentDisplayName (n.program).toRawUTF8()));
    }

    return 0;
}
