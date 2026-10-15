#include "SimpleSynth.h"

#include <cmath>

namespace atm
{

namespace
{
    constexpr double kPi = juce::MathConstants<double>::pi;
    constexpr float  kLevel = 0.11f;   // per voice, before the envelope

    // Drum kinds, from the GM percussion note.
    enum DrumKind { kick, snare, hat, tom, cymbal, other };

    DrumKind drumKindFor (int pitch) noexcept
    {
        switch (pitch)
        {
            case 35: case 36:                                   return kick;
            case 37: case 38: case 39: case 40:                 return snare;
            case 42: case 44: case 46:                          return hat;
            case 41: case 43: case 45: case 47: case 48: case 50: return tom;
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return cymbal;
            default:                                            return other;
        }
    }

    float nextNoise (juce::uint32& state) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
}

void SimpleSynth::noteOn (int layer, int pitch, bool drum, int velocity)
{
    // Steal the oldest voice if none is free.
    Voice* chosen = nullptr;
    for (auto& voice : voices)
        if (! voice.active) { chosen = &voice; break; }
    if (chosen == nullptr)
    {
        chosen = &voices[0];
        for (auto& voice : voices)
            if (voice.order < chosen->order)
                chosen = &voice;
    }

    auto& v = *chosen;
    v.active = true;
    v.released = false;
    v.layer = layer;
    v.pitch = pitch;
    v.drum = drum;
    v.drumKind = drumKindFor (pitch);
    v.order = ++counter;
    v.gain = (float) juce::jlimit (1, 127, velocity) / 100.0f;
    v.phase = 0.0;
    v.env = 0.0;
    v.freq = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    v.noise = ((juce::uint32) pitch * 2654435761u) | 1u;
}

void SimpleSynth::noteOff (int layer, int pitch, bool)
{
    for (auto& voice : voices)
        if (voice.active && voice.layer == layer && voice.pitch == pitch)
            voice.released = true;
}

void SimpleSynth::releaseAll()
{
    for (auto& voice : voices)
        voice.released = true;
}

void SimpleSynth::cutAll()
{
    for (auto& voice : voices)
        voice.active = false;
}

bool SimpleSynth::isSilent() const
{
    for (const auto& voice : voices)
        if (voice.active)
            return false;
    return true;
}

float SimpleSynth::renderVoice (Voice& v, double rate)
{
    // Time since the hit, in seconds, for the drum shapes.
    const double t = v.phase;

    if (v.drum)
    {
        float out = 0.0f;
        double amp = 0.0, tau = 0.1;

        switch (v.drumKind)
        {
            case kick:
            {
                const double f = 45.0 + 90.0 * std::exp (-t / 0.045);
                v.env += 2.0 * kPi * f / rate;               // env doubles as the sine phase
                out = (float) std::sin (v.env);
                tau = 0.14;
                break;
            }
            case snare:
                out = 0.7f * nextNoise (v.noise) + 0.3f * (float) std::sin (2.0 * kPi * 190.0 * t);
                tau = 0.08;
                break;
            case hat:
            {
                // Differentiated noise: bright, thin.
                const float n = nextNoise (v.noise);
                out = n - (float) v.env;
                v.env = n;
                tau = v.pitch == 46 ? 0.18 : v.pitch == 44 ? 0.05 : 0.035;
                break;
            }
            case tom:
                out = (float) std::sin (2.0 * kPi * (80.0 + 12.0 * (v.pitch - 41)) * t * (1.0 + 0.3 * std::exp (-t / 0.05)));
                tau = 0.16;
                break;
            case cymbal:
            {
                const float n = nextNoise (v.noise);
                out = 0.5f * (n - (float) v.env);   // differentiated, like the hats, but long
                v.env = n;
                tau = 0.5;
                break;
            }
            default:
                out = nextNoise (v.noise);
                tau = 0.07;
                break;
        }

        amp = std::exp (-t / tau);
        v.phase += 1.0 / rate;
        if (amp < 0.002)
            v.active = false;
        return (float) (amp * 0.9) * out;
    }

    // Pitched: three harmonics. A 3 ms linear attack, then a one-pole decay
    // to a sustain, then a one-pole release once the note ends (or its layer
    // is silenced).
    const double attack = 0.003, decayTau = 0.18, sustain = 0.55, releaseTau = 0.05;

    if (v.released)
        v.env += (0.0 - v.env) * (1.0 - std::exp (-1.0 / (releaseTau * rate)));
    else if (t < attack)
        v.env = juce::jmin (1.0, v.env + 1.0 / (attack * rate));
    else
        v.env += (sustain - v.env) * (1.0 - std::exp (-1.0 / (decayTau * rate)));

    v.phase += 1.0 / rate;
    const double ph = 2.0 * kPi * v.freq * v.phase;
    const float wave = (float) (std::sin (ph) + 0.5 * std::sin (2.0 * ph) + 0.25 * std::sin (3.0 * ph)) * 0.6f;

    if (v.released && v.env < 0.001)
        v.active = false;

    return (float) v.env * wave;
}

void SimpleSynth::render (float* left, float* right, int numSamples, double sampleRate)
{
    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = kLevel * voice.gain * renderVoice (voice, sampleRate);
            left[i] += sample;
            right[i] += sample;
            if (! voice.active)
                break;
        }
    }
}

} // namespace atm
