#include "SoundFont.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

// TinySoundFont is C89 written for other people's warning settings.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define TSF_IMPLEMENTATION
#include "tsf.h"
#pragma clang diagnostic pop

namespace atm
{

namespace
{
    constexpr int kDrumLayer = 128;

    // The bank's level against the original audio (normalised to 0.9 peak):
    // a single piano note at velocity 100 peaks at -6.7 dBFS unscaled, and
    // chords stack. Calibrated with `atm-cli --preview-render` so the
    // song excerpt's rendering sits at about the same loudness as the audio.
    constexpr float kGainDb = -6.0f;

    constexpr int kScratchSamples = 1024;
}

juce::File soundFontFile()
{
    // The installer puts the bank in the machine-wide folder (bundled, not
    // downloaded, so the preview sounds right at once); build.sh fetches it into the
    // user's own folder on a development machine. The installer's copy wins.
    const auto inFolder = [] (juce::File::SpecialLocationType where)
    {
        return juce::File::getSpecialLocation (where)
                   .getChildFile ("Application Support/AudioFlower/soundfonts/GeneralUser-GS.sf2");
    };

    const auto installed = inFolder (juce::File::commonApplicationDataDirectory);   // /Library
    const auto fetched   = inFolder (juce::File::userApplicationDataDirectory);     // ~/Library
    return installed.existsAsFile() || ! fetched.existsAsFile() ? installed : fetched;
}

//==============================================================================
SoundFont::SoundFont (tsf* f, juce::String n)
    : font (f), name (std::move (n))
{
    for (int layer = 0; layer < (int) presets.size(); ++layer)
        presets[(size_t) layer] = tsf_get_presetindex (font, 0, layer < kDrumLayer ? layer : 0);

    // Anything the bank lacks plays as a piano rather than nothing.
    for (int layer = 0; layer < kDrumLayer; ++layer)
        if (presets[(size_t) layer] < 0)
            presets[(size_t) layer] = presets[0];

    // Drums: the standard kit, or whatever kit the bank has.
    int kit = tsf_get_presetindex (font, 128, 0);
    for (int i = 0; kit < 0 && i < font->presetNum; ++i)
        if (font->presets[i].bank == 128)
            kit = i;

    presets[kDrumLayer] = presets[kDrumLayer + 1] = kit;
}

SoundFont::~SoundFont()
{
    tsf_close (font);
}

std::shared_ptr<const SoundFont> SoundFont::load (const juce::File& file)
{
    if (! file.existsAsFile())
        return nullptr;

    tsf* font = tsf_load_filename (file.getFullPathName().toRawUTF8());
    if (font == nullptr)
    {
        juce::Logger::writeToLog ("soundfont: couldn't read " + file.getFullPathName());
        return nullptr;
    }

    return std::shared_ptr<const SoundFont> (new SoundFont (font, file.getFileNameWithoutExtension()));
}

int SoundFont::presetFor (int layer) const noexcept
{
    return presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, layer)];
}

//==============================================================================
SoundFontLibrary::SoundFontLibrary()
    : juce::Thread ("soundfont load")
{
    startThread (juce::Thread::Priority::low);
}

SoundFontLibrary::~SoundFontLibrary()
{
    // Before the thread is joined: a run() that finishes during the join
    // must not post to a half-destroyed object.
    masterReference.clear();
    stopThread (-1);   // unbounded: a load can't be interrupted, and takes well under a second
}

std::shared_ptr<const SoundFont> SoundFontLibrary::get() const
{
    const juce::SpinLock::ScopedLockType scoped (lock);
    return font;
}

void SoundFontLibrary::run()
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto loaded = SoundFont::load (soundFontFile());

    if (loaded != nullptr)
        juce::Logger::writeToLog ("soundfont: " + loaded->getName() + " loaded in "
                                  + juce::String (juce::Time::getMillisecondCounterHiRes() - started, 0) + " ms");
    else
        juce::Logger::writeToLog ("soundfont: none at " + soundFontFile().getFullPathName() + ", the preview uses the simple synth");

    {
        const juce::SpinLock::ScopedLockType scoped (lock);
        font = std::move (loaded);
    }

    juce::MessageManager::callAsync ([safeThis = juce::WeakReference<SoundFontLibrary> (this)]
    {
        if (auto* self = safeThis.get())
            self->sendChangeMessage();
    });
}

//==============================================================================
SoundFontSynth::SoundFontSynth (std::shared_ptr<const SoundFont> f)
    : font (std::move (f)), scratch ((size_t) kScratchSamples * 2, 0.0f)
{
    // A copy shares the presets and samples and gets its own voices,
    // allocated here, once: with a maximum set, note-on never allocates.
    player = tsf_copy (font->handle());
    tsf_set_max_voices (player, kMaxVoices);
}

SoundFontSynth::~SoundFontSynth()
{
    if (player != nullptr)
        tsf_close (player);
}

void SoundFontSynth::noteOn (int layer, int pitch, bool drum, int velocity)
{
    const int preset = font->presetFor (drum ? kDrumLayer : layer);
    if (preset >= 0)
        tsf_note_on (player, preset, pitch, (float) juce::jlimit (1, 127, velocity) / 127.0f);
}

void SoundFontSynth::noteOff (int layer, int pitch, bool drum)
{
    const int preset = font->presetFor (drum ? kDrumLayer : layer);
    if (preset >= 0)
        tsf_note_off (player, preset, pitch);
}

void SoundFontSynth::releaseAll()
{
    tsf_note_off_all (player);
}

void SoundFontSynth::cutAll()
{
    tsf_reset (player);
}

bool SoundFontSynth::isSilent() const
{
    return tsf_active_voice_count (player) == 0;
}

void SoundFontSynth::render (float* left, float* right, int numSamples, double sampleRate)
{
    if (! juce::exactlyEqual (sampleRate, currentRate))
    {
        // Just stores the rate and gain; safe here.
        tsf_set_output (player, TSF_STEREO_UNWEAVED, (int) std::lround (sampleRate), kGainDb);
        currentRate = sampleRate;
    }

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (kScratchSamples, numSamples - done);
        tsf_render_float (player, scratch.data(), n, 0);

        juce::FloatVectorOperations::add (left + done, scratch.data(), n);
        juce::FloatVectorOperations::add (right + done, scratch.data() + n, n);
        done += n;
    }
}

} // namespace atm
