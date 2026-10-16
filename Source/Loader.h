// Loader.h — decodificación de temas y samples en segundo plano.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include "Deck.h"

namespace ct
{

// Un hilo dedicado: decodifica a 16 bits en bloques y va publicando el avance,
// así el deck puede empezar a sonar antes de que termine.
class TrackLoader
{
public:
    TrackLoader() : pool (1)
    {
        formats.registerBasicFormats();
    }

    ~TrackLoader() { pool.removeAllJobs (true, 4000); }

    juce::AudioFormatManager& getFormats() { return formats; }

    juce::String getWildcard() const { return formats.getWildcardForAllFormats(); }

    std::shared_ptr<TrackAudio> load (const juce::File& file)
    {
        auto track = std::make_shared<TrackAudio>();
        track->title = file.getFileNameWithoutExtension().toStdString();
        track->path = file.getFullPathName().toStdString();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            track->failed.store (true);
            track->finished.store (true);
            return track;
        }

        // margen de 2 s por si el largo informado (mp3) es aproximado
        const auto frames = reader->lengthInSamples + (juce::int64) (reader->sampleRate * 2.0);
        track->allocate (frames, (int) reader->numChannels, (float) reader->sampleRate);
        track->lengthFrames.store (reader->lengthInSamples);

        pool.addJob ([track, r = reader.release()]
        {
            std::unique_ptr<juce::AudioFormatReader> rd (r);
            decode (*track, *rd);
        });
        return track;
    }

private:
    static void decode (TrackAudio& t, juce::AudioFormatReader& reader)
    {
        constexpr int chunk = 32768;
        juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader.numChannels), chunk);
        const juce::int64 capacity = (juce::int64) t.left.size();
        juce::int64 pos = 0;
        float lowState = 0.0f;
        const float lowCoef = std::exp (-2.0f * 3.14159265f * 150.0f / (float) reader.sampleRate);
        float pk = 0.0f, pkLow = 0.0f;
        int pkCount = 0;
        size_t pkIndex = 0;

        while (pos < reader.lengthInSamples && ! t.cancel.load())
        {
            const int n = (int) juce::jmin ((juce::int64) chunk, reader.lengthInSamples - pos, capacity - pos);
            if (n <= 0) break;
            buf.clear();
            if (! reader.read (&buf, 0, n, pos, true, true)) break;

            const float* l = buf.getReadPointer (0);
            const float* r = reader.numChannels > 1 ? buf.getReadPointer (1) : l;
            int16_t* dl = t.left.data() + pos;
            int16_t* dr = t.right.data() + pos;
            for (int i = 0; i < n; ++i)
            {
                dl[i] = (int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (l[i] * 32767.0f));
                dr[i] = (int16_t) juce::jlimit (-32767, 32767, juce::roundToInt (r[i] * 32767.0f));

                const float m = 0.5f * (l[i] + r[i]);
                lowState = m + lowCoef * (lowState - m);
                pk = juce::jmax (pk, std::abs (m));
                pkLow = juce::jmax (pkLow, std::abs (lowState) * 1.6f);
                if (++pkCount == TrackAudio::kPeakFrames)
                {
                    if (pkIndex < t.peaks.size())
                    {
                        t.peaks[pkIndex] = (uint8_t) juce::jlimit (0, 255, juce::roundToInt (std::sqrt (pk) * 255.0f));
                        t.peaksLow[pkIndex] = (uint8_t) juce::jlimit (0, 255, juce::roundToInt (std::sqrt (juce::jmin (1.0f, pkLow)) * 255.0f));
                    }
                    ++pkIndex; pk = pkLow = 0.0f; pkCount = 0;
                }
            }
            pos += n;
            t.decodedFrames.store (pos, std::memory_order_release);
        }

        if (! t.cancel.load())
            t.lengthFrames.store (juce::jmax ((juce::int64) 1, pos));
        t.finished.store (true);
    }

    juce::AudioFormatManager formats;
    juce::ThreadPool pool;
};

} // namespace ct
