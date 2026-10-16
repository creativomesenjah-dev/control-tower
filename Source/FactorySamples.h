// FactorySamples.h — 8 one-shots de dub sintetizados (sin samples de terceros).
// Se generan como WAV la primera vez y quedan cargados en las ranuras 1 a 8.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Dsp.h"

namespace ct::factory
{

constexpr int kSr = 44100;

inline std::vector<float> make (const juce::String& name)
{
    std::vector<float> out;
    juce::Random rnd (42);
    auto noise = [&] { return rnd.nextFloat() * 2.0f - 1.0f; };
    auto secs = [&] (float s) { out.assign ((size_t) (s * kSr), 0.0f); };
    const float tw = 2.0f * kPi;

    if (name == "AIRHORN")
    {
        // tres bocinazos: corto, corto, largo
        secs (1.6f);
        const float starts[3] = { 0.0f, 0.26f, 0.52f }, lens[3] = { 0.2f, 0.2f, 0.95f };
        SVF2 lp; lp.set (3200.0f, (float) kSr, 0.9f);
        double ph[4] {};
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < (int) (lens[b] * kSr); ++i)
            {
                const auto idx = (size_t) (starts[b] * kSr) + (size_t) i;
                if (idx >= out.size()) break;
                const float t = (float) i / kSr;
                const float env = std::min (1.0f, t / 0.012f) * std::min (1.0f, (lens[b] - t) / 0.04f);
                const float bend = 1.0f - 0.06f * std::exp (-t * 30.0f) + 0.004f * std::sin (tw * 6.0f * t);
                float s = 0.0f;
                const float det[4] = { 1.0f, 1.004f, 0.996f, 2.002f };
                for (int k = 0; k < 4; ++k)
                {
                    ph[k] += 466.0 * det[k] * bend / kSr;
                    ph[k] -= std::floor (ph[k]);
                    s += (2.0f * (float) ph[k] - 1.0f) * (k == 3 ? 0.4f : 1.0f);
                }
                out[idx] = lp.lowpass (std::tanh (s * 0.8f)) * env;
            }
    }
    else if (name == "DISPARO")
    {
        secs (1.5f);
        SVF2 bp; bp.set (1800.0f, (float) kSr, 0.8f);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            const float crack = noise() * std::exp (-t * 38.0f);
            const float tail = bp.lowpass (noise()) * 0.35f * std::exp (-t * 3.5f);
            const float thump = std::sin (tw * (55.0f * t + 40.0f * (1.0f - std::exp (-t * 25.0f)) / 25.0f)) * std::exp (-t * 9.0f);
            out[i] = std::tanh (1.6f * (crack + tail + thump));
        }
    }
    else if (name == "LASER")
    {
        secs (0.45f);
        double ph = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            const float f = 150.0f + 2600.0f * std::exp (-t * 11.0f);
            ph += f / kSr; ph -= std::floor (ph);
            out[i] = (ph < 0.5 ? 0.6f : -0.6f) * std::min (1.0f, t / 0.003f) * std::exp (-t * 4.0f);
        }
    }
    else if (name == "BOMBA")
    {
        // silbido que cae + explosión
        secs (3.6f);
        double ph = 0.0;
        SVF2 lp; lp.set (900.0f, (float) kSr, 0.7f);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            float s = 0.0f;
            if (t < 1.8f)
            {
                const float f = 2200.0f * std::pow (400.0f / 2200.0f, t / 1.8f) * (1.0f + 0.01f * std::sin (tw * 7.0f * t));
                ph += f / kSr; ph -= std::floor (ph);
                s = 0.35f * std::sin (tw * (float) ph) * std::min (1.0f, t / 0.1f);
            }
            else
            {
                const float u = t - 1.8f;
                s = lp.lowpass (noise()) * 1.6f * std::exp (-u * 2.2f)
                  + std::sin (tw * 42.0f * u) * std::exp (-u * 2.0f);
            }
            out[i] = std::tanh (s);
        }
    }
    else if (name == "SUB BOOM")
    {
        secs (2.0f);
        double ph = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            const float f = 45.0f + 75.0f * std::exp (-t * 30.0f);
            ph += f / kSr;
            out[i] = std::tanh (1.4f * std::sin (tw * (float) ph)) * std::exp (-t * 1.8f);
        }
    }
    else if (name == "RISER")
    {
        secs (3.2f);
        SVF2 bp;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            if (i % 32 == 0) bp.set (200.0f * std::pow (40.0f, t / 3.2f), (float) kSr, 3.0f);
            float l, b, h; bp.tick (noise(), l, b, h);
            out[i] = b * (t / 3.2f) * (t / 3.2f) * 1.5f;
        }
    }
    else if (name == "SNARE")
    {
        secs (0.6f);
        SVF2 hp; hp.set (900.0f, (float) kSr, 0.7f);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            out[i] = 0.8f * hp.highpass (noise()) * std::exp (-t * 12.0f)
                   + 0.6f * std::sin (tw * 190.0f * t) * std::exp (-t * 20.0f);
        }
    }
    else if (name == "SIRENA CORTA")
    {
        secs (1.2f);
        double ph = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            const float f = (std::fmod (t, 0.3f) < 0.15f) ? 740.0f : 990.0f;
            ph += f / kSr; ph -= std::floor (ph);
            out[i] = (ph < 0.5 ? 0.5f : -0.5f) * std::min (1.0f, (1.2f - t) / 0.1f);
        }
    }

    float pk = 0.0f;
    for (float v : out) pk = std::max (pk, std::abs (v));
    if (pk > 0.0f) for (auto& v : out) v *= 0.8f / pk;
    return out;
}

inline const juce::StringArray& names()
{
    static const juce::StringArray n { "AIRHORN", "DISPARO", "LASER", "BOMBA", "SUB BOOM", "RISER", "SNARE", "SIRENA CORTA" };
    return n;
}

// Escribe los 8 WAV en la carpeta (si no existen) y devuelve los archivos.
inline juce::Array<juce::File> install (const juce::File& dir)
{
    juce::Array<juce::File> files;
    dir.createDirectory();
    for (const auto& n : names())
    {
        const auto f = dir.getChildFile (n + ".wav");
        if (! f.existsAsFile())
        {
            const auto data = make (n);
            juce::AudioBuffer<float> b (2, (int) data.size());
            b.copyFrom (0, 0, data.data(), (int) data.size());
            b.copyFrom (1, 0, data.data(), (int) data.size());
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
            juce::WavAudioFormat wav;
            if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kSr).withNumChannels (2).withBitsPerSample (16)))
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        files.add (f);
    }
    return files;
}

} // namespace ct::factory
