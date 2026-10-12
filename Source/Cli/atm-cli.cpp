// atm-cli: decode -> resample -> transcribe from a terminal, through the same
// code the plugin uses.
//
//   atm-cli <audio file> [small|medium|large] [--cpu] [--notes N] [--midi DIR [--bpm B]]
//           [--quantize PCT] [--grid 4|8|16] [--preview-render OUT.wav]
//
// Prints the device the model ran on, timings, a per-instrument count and
// the first N notes (default 20). --midi writes one .mid per layer into DIR,
// conformed to the detected tempo as the plugin does (--bpm is the host
// tempo written into the file, default 120), reads each back and reports
// the worst onset error. The cleanup flags run the same cleanup the plugin
// does before export; with --quantize 100 the check also confirms every
// onset sits on the grid. Exit code 0 on success, 1 on failure, 2 on
// usage. The model must already be installed (the plugin downloads it).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "Engine/Cleanup.h"
#include "Engine/MidiExport.h"
#include "Engine/ModelFiles.h"
#include "Engine/Preview.h"
#include "Engine/Resampler.h"
#include "Engine/Tempo.h"
#include "Engine/Velocity.h"   // research only; not in the plugin
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
        print ("usage: atm-cli <audio file> [small|medium|large] [--cpu] [--notes N] [--midi DIR [--bpm B]]");
        print ("               [--quantize PCT] [--grid 4|8|16] [--preview-render OUT.wav]");
        print ("       atm-cli --resample-check     (alignment test, no model needed)");
        print ("       atm-cli --tempo-check        (synthetic click tracks, no model needed)");
        print ("       atm-cli --velocity-check     (synthetic tones at known levels, no model needed)");
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

namespace
{
    /** Click tracks at known tempos, with accents every four beats: the
        detector must find the tempo within 0.1 BPM, the beat phase within
        15 ms and the accented beat as the downbeat. */
    int tempoCheck()
    {
        struct Case { double bpm, phase; bool withNotes; };
        const Case cases[] = { { 97.3, 0.37, false }, { 72.0, 0.0, false }, { 140.0, 0.21, false },
                               { 120.0, 0.5, false }, { 88.6, 0.62, true } };
        bool ok = true;
        juce::Random random (7);

        for (const auto& c : cases)
        {
            constexpr double seconds = 30.0;
            const int rate = (int) atm::kModelSampleRate;
            std::vector<float> audio ((size_t) (seconds * rate), 0.0f);
            std::vector<msl::Note> notes;
            const double period = 60.0 / c.bpm;

            // A faint noise floor, then a short burst per beat, louder on downbeats.
            for (auto& x : audio)
                x = 0.01f * (random.nextFloat() * 2.0f - 1.0f);

            for (int k = 0; c.phase + k * period < seconds; ++k)
            {
                const double t = c.phase + k * period;
                const float gain = (k % 4 == 0) ? 1.0f : 0.5f;
                const int start = (int) (t * rate);
                for (int i = 0; i < rate / 100 && start + i < (int) audio.size(); ++i)
                    audio[(size_t) (start + i)] += gain * (random.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / ((float) rate * 0.003f));

                if (c.withNotes)
                    notes.push_back ({ t, t + 0.01, (k % 4 == 0) ? 36 : 38, msl::DRUM_PROGRAM, true });
            }

            const auto est = atm::estimateTempo (audio, notes);

            const double bpmError = std::abs (est.bpm - c.bpm);
            double phaseError = std::fmod (std::abs (est.firstBeat - c.phase), period);
            phaseError = juce::jmin (phaseError, period - phaseError);
            // The accents sit at phase + 4k * period: the downbeat beat must land there.
            double downbeatError = std::fmod (std::abs (est.beatTime (est.downbeat) - c.phase), 4.0 * period);
            downbeatError = juce::jmin (downbeatError, 4.0 * period - downbeatError);

            const bool pass = bpmError <= 0.1 && phaseError <= 0.015 && downbeatError <= 0.015;
            ok = ok && pass;

            print (juce::String::formatted ("%6.1f BPM @ %.2f s%s -> %6.2f BPM, first beat %.3f s, downbeat %d, conf %.2f: %s",
                                            c.bpm, c.phase, c.withNotes ? " +notes" : "       ", est.bpm, est.firstBeat,
                                            est.downbeat, est.confidence, pass ? "ok" : "WRONG"));
        }

        return ok ? 0 : 1;
    }
}

namespace
{
    /** Two overlapping parts: a melody stepping up in loudness, over a
        steady-level bass on other pitches. The melody's velocities must rise
        with its level (the bass must not leak into them), and the bass,
        steady, must come out at one velocity give or take. */
    int velocityCheck()
    {
        constexpr double rate = 44100.0;
        SourceAudio audio;
        audio.sampleRate = rate;
        audio.samples.setSize (1, (int) (10.0 * rate));
        audio.samples.clear();
        std::vector<msl::Note> notes;

        const auto addTone = [&] (double onset, int pitch, float gain, int program)
        {
            const double f = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
            const int start = (int) (onset * rate), len = (int) (0.4 * rate);
            for (int i = 0; i < len; ++i)
            {
                const double env = std::exp (-i / (0.25 * rate));
                audio.samples.addSample (0, start + i, gain * (float) (env * (std::sin (2.0 * juce::MathConstants<double>::pi * f * i / rate)
                                                                              + 0.4 * std::sin (4.0 * juce::MathConstants<double>::pi * f * i / rate))));
            }
            notes.push_back ({ onset, onset + 0.4, pitch, program, false });
        };

        const float melodyGains[] = { 0.02f, 0.05f, 0.1f, 0.2f, 0.4f, 0.8f };
        for (int i = 0; i < 6; ++i)
        {
            addTone (0.5 + i * 1.5, 72 + (i % 3) * 2, melodyGains[i], 0);   // piano melody
            addTone (0.5 + i * 1.5, 40, 0.3f, 33);                            // steady bass underneath
        }

        const auto velocities = atm::velocitiesFromLevels (notes, atm::measureLevels (audio, notes));

        juce::String melody, bass;
        int lastMelody = 0, bassLo = 127, bassHi = 0;
        bool rising = true;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const int v = velocities[i];
            if (notes[i].program == 0)
            {
                melody << v << " ";
                rising = rising && v > lastMelody;
                lastMelody = v;
            }
            else
            {
                bass << v << " ";
                bassLo = juce::jmin (bassLo, v);
                bassHi = juce::jmax (bassHi, v);
            }
        }

        // A steady part has no real spread to stretch over 40-120: the
        // per-layer mapping only kicks in past 0.5 dB, else it sits at 80.
        const bool bassSteady = bassHi - bassLo <= 12;
        print ("melody (rising level):  " + melody + (rising ? " ok" : " WRONG"));
        print ("bass   (steady level):  " + bass + (bassSteady ? " ok" : " WRONG"));
        return rising && bassSteady ? 0 : 1;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager, for the service's WeakReferences

    if (argc == 2 && juce::String (argv[1]) == "--resample-check")
        return resampleCheck();
    if (argc == 2 && juce::String (argv[1]) == "--tempo-check")
        return tempoCheck();
    if (argc == 2 && juce::String (argv[1]) == "--velocity-check")
        return velocityCheck();

    juce::File file;
    auto model = atm::kDefaultModelSize;
    bool forceCpu = false;
    int notesToPrint = 20;
    juce::File midiDir;
    double bpm = 120.0;
    atm::CleanupSettings cleanup;
    juce::File previewOut;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--cpu")
            forceCpu = true;
        else if (arg == "--notes" && i + 1 < argc)
            notesToPrint = juce::String (argv[++i]).getIntValue();
        else if (arg == "--midi" && i + 1 < argc)
            midiDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (arg == "--bpm" && i + 1 < argc)
            bpm = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--preview-render" && i + 1 < argc)
            previewOut = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
        else if (arg == "--quantize" && i + 1 < argc)
            cleanup.quantize = juce::String (argv[++i]).getDoubleValue() / 100.0;
        else if (arg == "--grid" && i + 1 < argc)
        {
            const int g = juce::String (argv[++i]).getIntValue();
            cleanup.grid = g == 4 ? atm::Grid::quarter : g == 8 ? atm::Grid::eighth : atm::Grid::sixteenth;
        }
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
    std::map<int, int> perLayer;
    for (const auto& note : notes)
        ++perLayer[atm::layerKeyFor (note)];

    print ("notes:    " + juce::String (notes.size()) + " in " + juce::String (perLayer.size()) + " layer(s)");
    for (const auto& [layer, count] : perLayer)
        print ("  " + atm::layerName (layer).paddedRight (' ', 26) + juce::String (count));

    // 4. Tempo, from the audio and the notes together.
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    const auto est = atm::estimateTempo (atm::resampleForModel (audio), notes);
    print (juce::String::formatted ("tempo:    %.2f BPM, first beat %.3f s, downbeat %d, confidence %.2f (%.0f ms)",
                                    est.bpm, est.firstBeat, est.downbeat, est.confidence,
                                    juce::Time::getMillisecondCounterHiRes() - t1));

    // 5. Cleanup -- what the plugin shows and exports.
    const auto cleaned = atm::cleanUp (notes, cleanup, est);
    print (juce::String::formatted ("cleanup:  quantize %.0f%% %s", cleanup.quantize * 100.0, atm::gridName (cleanup.grid).toRawUTF8()));

    atm::ExportTiming timing;
    timing.hostBpm = bpm;
    timing.conform = true;
    timing.tempo = est;
    timing.earliestOnset = atm::earliestOnsetOf (cleaned);

    // 6. Preview: render the synth (mix 100%) offline through the same
    //    class the plugin plays with, and check it made sound where the notes
    //    are and nothing else.
    if (previewOut != juce::File())
    {
        constexpr double rate = 48000.0;
        constexpr int block = 512;
        atm::Preview preview;
        preview.prepare (rate, block);
        preview.setSource (decoded.audio);
        preview.setNotes (cleaned);
        preview.setMix (1.0f);
        preview.play();

        const int total = (int) ((audio.getLengthSeconds() + 1.0) * rate);
        juce::AudioBuffer<float> rendered (2, total);
        rendered.clear();
        juce::AudioBuffer<float> chunk (2, block);
        int written = 0;
        while (written < total && preview.isPlaying())
        {
            preview.render (chunk);
            const int n = juce::jmin (block, total - written);
            for (int ch = 0; ch < 2; ++ch)
                rendered.copyFrom (ch, written, chunk, ch, 0, n);
            written += n;
        }

        int firstLoud = -1;
        bool finite = true;
        float peak = 0.0f;
        for (int i = 0; i < written; ++i)
        {
            const float v = rendered.getSample (0, i);
            finite = finite && std::isfinite (v);
            peak = juce::jmax (peak, std::abs (v));
            if (firstLoud < 0 && std::abs (v) > 0.01f)
                firstLoud = i;
        }

        const double firstOnset = atm::earliestOnsetOf (cleaned);
        const double firstSound = firstLoud >= 0 ? firstLoud / rate : -1.0;
        const bool ok = finite && peak > 0.05f && peak <= 1.0f && firstLoud >= 0 && std::abs (firstSound - firstOnset) < 0.02;

        previewOut.deleteFile();
        if (std::unique_ptr<juce::OutputStream> stream = previewOut.createOutputStream())
        {
            juce::WavAudioFormat wav;
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                                                           .withNumChannels (2)
                                                                                           .withBitsPerSample (16)))
                writer->writeFromAudioSampleBuffer (rendered, 0, written);
        }

        print (juce::String::formatted ("preview:  %.1f s rendered, peak %.2f (must be 0.05-1.0), first sound at %.3f s (first note %.3f s)%s: %s",
                                        written / rate, peak, firstSound, firstOnset, finite ? "" : ", NOT FINITE", ok ? "ok" : "WRONG"));
        if (! ok)
            return 1;
    }

    // 7. MIDI per layer, read back to check the timing survives the round trip.
    if (midiDir != juce::File())
    {
        midiDir.createDirectory();
        bool allOk = true;

        print (juce::String::formatted ("midi:     conformed to %.2f BPM, %d beat(s) of lead-in, first downbeat at bar %d",
                                            est.bpm, timing.leadInBeats(), (timing.leadInBeats() + est.downbeat) / est.beatsPerBar + 1));

        for (const auto& [layer, count] : perLayer)
        {
            const auto out = midiDir.getChildFile (atm::layerMidiFileName (audio.fileName, layer));
            if (! atm::writeMidiFile (atm::buildLayerMidi (cleaned, layer, timing), out))
            {
                print ("midi:     couldn't write " + out.getFullPathName());
                allOk = false;
                continue;
            }

            juce::MidiFile readBack;
            juce::FileInputStream in (out);
            readBack.readFrom (in);

            // Grid check on the ticks, before converting to seconds: with full
            // quantize every note-on must sit on a grid division.
            int onGrid = 0, noteOns = 0;
            {
                const int division = atm::kTicksPerQuarter / (int) cleanup.grid;
                const auto* ticks = readBack.getTrack (0);
                for (int e = 0; ticks != nullptr && e < ticks->getNumEvents(); ++e)
                    if (ticks->getEventPointer (e)->message.isNoteOn())
                    {
                        ++noteOns;
                        const int t = juce::roundToInt (ticks->getEventPointer (e)->message.getTimeStamp());
                        if (t % division <= 1 || division - t % division <= 1)
                            ++onGrid;
                    }
            }
            readBack.convertTimestampTicksToSeconds();

            std::vector<double> got;
            const auto* track = readBack.getTrack (0);
            for (int e = 0; track != nullptr && e < track->getNumEvents(); ++e)
                if (track->getEventPointer (e)->message.isNoteOn())
                    got.push_back (track->getEventPointer (e)->message.getTimeStamp());

            // Where each onset should land, in the file's seconds (its tempo
            // meta is the host tempo, which convertTimestampTicksToSeconds used).
            std::vector<double> want;
            for (const auto& note : cleaned)
                if (note.layer == layer)
                    want.push_back (juce::jmax (0.0, atm::tickFor (note.onset, timing)) / atm::kTicksPerQuarter * 60.0 / bpm);

            std::sort (got.begin(), got.end());
            std::sort (want.begin(), want.end());
            want.erase (std::unique (want.begin(), want.end(), [] (double a, double b) { return std::abs (a - b) < 1e-9; }), want.end());

            double worst = 0.0;
            for (const double t : got)
            {
                const auto it = std::lower_bound (want.begin(), want.end(), t - 1.0);
                double best = 1e9;
                for (auto j = it; j != want.end() && *j <= t + 1.0; ++j)
                    best = std::min (best, std::abs (*j - t));
                worst = std::max (worst, best);
            }

            print (juce::String::formatted ("midi:     %-44s %4d note-ons (%d notes), worst onset error %.2f ms, %d/%d on the %s grid",
                                            out.getFileName().toRawUTF8(), (int) got.size(), count, worst * 1000.0,
                                            onGrid, noteOns, atm::gridName (cleanup.grid).toRawUTF8()));
            allOk = allOk && worst < 0.002 && (cleanup.quantize < 1.0 || onGrid == noteOns);
        }

        if (! allOk)
            return 1;
    }

    std::sort (notes.begin(), notes.end(), [] (const msl::Note& a, const msl::Note& b) { return a.onset < b.onset; });

    for (int i = 0; i < juce::jmin (notesToPrint, (int) notes.size()); ++i)
    {
        const auto& n = notes[(size_t) i];
        print (juce::String::formatted ("  %7.3f - %7.3f  pitch %3d  %s", n.onset, n.offset, n.pitch,
                                        atm::instrumentDisplayName (n.program).toRawUTF8()));
    }

    return 0;
}
