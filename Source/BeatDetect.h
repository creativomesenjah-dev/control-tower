// BeatDetect.h — detección de BPM y de la grilla de beats de un tema ya decodificado. Sin JUCE.
//
// 1) Envolvente de "ataques": subida de energía (graves y total) cada ~11 ms.
// 2) Autocorrelación de esa envolvente entre 58 y 120 BPM (el rango donde se cuenta el reggae / dub;
//    evita que el skank a contratiempo lo haga leer al doble). x2 / ÷2 en el deck si hace falta.
// 3) Ajuste fino del BPM (±2 %) y de la fase con un "peine" sobre todo el tramo analizado:
//    da el primer beat, así LOOP IN / OUT se acomodan al beat más cercano.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ct
{

struct BeatGrid
{
    double bpm = 0.0;          // 0 = no se pudo detectar
    double firstBeat = 0.0;    // en cuadros del tema
};

inline BeatGrid detectBeatGrid (const int16_t* L, const int16_t* R, int64_t frames, double sr)
{
    BeatGrid g;
    if (frames < (int64_t) (sr * 8.0)) return g;

    const int hop = std::max (64, (int) std::lround (sr / 86.0));    // ~11,6 ms
    // analizar hasta 150 s desde el principio (salteando los primeros 2 s si el tema es largo)
    const int64_t start = frames > (int64_t) (sr * 40.0) ? (int64_t) (sr * 2.0) : 0;
    const int64_t end = std::min (frames, start + (int64_t) (sr * 150.0));
    const int nHops = (int) ((end - start) / hop);
    if (nHops < 400) return g;

    std::vector<float> onset ((size_t) nHops, 0.0f);
    const float aLow = 1.0f - std::exp (-2.0f * 3.14159265f * 150.0f / (float) sr);
    float lp = 0.0f, prevLow = 0.0f, prevAll = 0.0f;
    int64_t idx = start;
    for (int h = 0; h < nHops; ++h)
    {
        double eLow = 0.0, eAll = 0.0;
        for (int i = 0; i < hop; ++i, ++idx)
        {
            const float x = ((float) L[idx] + (float) R[idx]) * (1.0f / 65536.0f);
            lp += aLow * (x - lp);
            eLow += (double) lp * lp;
            eAll += (double) x * x;
        }
        const float l1 = std::log1p (1000.0f * (float) eLow / hop), l2 = std::log1p (1000.0f * (float) eAll / hop);
        onset[(size_t) h] = 1.5f * std::max (0.0f, l1 - prevLow) + std::max (0.0f, l2 - prevAll);
        prevLow = l1; prevAll = l2;
    }
    // sacar la media móvil (deja sólo los golpes)
    {
        std::vector<float> o2 (onset.size());
        const int w = 16;
        double acc = 0.0;
        for (int i = 0; i < nHops; ++i)
        {
            acc += onset[(size_t) i];
            if (i >= 2 * w) acc -= onset[(size_t) (i - 2 * w)];
            const int c = i - w;
            if (c >= 0) o2[(size_t) c] = std::max (0.0f, onset[(size_t) c] - (float) (acc / (2 * w)));
        }
        onset.swap (o2);
    }

    const double hopsPerSec = sr / hop;
    auto lagFor = [&] (double bpm) { return 60.0 * hopsPerSec / bpm; };

    // 2) autocorrelación con preferencia suave por 70-140 BPM
    double bestScore = -1.0, bestBpm = 0.0;
    // reggae / dub: se busca entre 58 y 120 BPM (el skank a contratiempo engaña a cualquier detector
    // hacia el doble; si un tema es más rápido, el BPM se corrige con x2 en el deck)
    for (double bpm = 58.0; bpm <= 120.0; bpm += 0.25)
    {
        const double lag = lagFor (bpm);
        const int l0 = (int) lag;
        const float fr = (float) (lag - l0);
        double s = 0.0;
        for (int i = 0; i + l0 + 1 < nHops; ++i)
            s += onset[(size_t) i] * ((1.0f - fr) * onset[(size_t) (i + l0)] + fr * onset[(size_t) (i + l0 + 1)]);
        // también premia que el doble del período (compás de 2) coincida
        const double weight = std::exp (-0.5 * std::pow (std::log2 (bpm / 82.0) / 0.6, 2.0));
        s *= 0.5 + weight;
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }
    if (bestBpm <= 0.0 || bestScore <= 0.0) return g;

    // 3) peine: BPM fino y fase
    double bestComb = -1.0, fineBpm = bestBpm, bestPhase = 0.0;
    for (double bpm = bestBpm * 0.985; bpm <= bestBpm * 1.015; bpm += 0.02)
    {
        const double period = lagFor (bpm);
        for (double ph = 0.0; ph < period; ph += 0.5)
        {
            double s = 0.0;
            for (double p = ph; p < nHops - 1; p += period)
            {
                const int i = (int) p;
                const float fr = (float) (p - i);
                s += (1.0f - fr) * onset[(size_t) i] + fr * onset[(size_t) (i + 1)];
            }
            if (s > bestComb) { bestComb = s; fineBpm = bpm; bestPhase = ph; }
        }
    }

    g.bpm = std::round (fineBpm * 100.0) / 100.0;
    const double beatFrames = 60.0 * sr / g.bpm;
    double first = (double) start + bestPhase * hop;
    first = std::fmod (first, beatFrames);
    if (first < 0.0) first += beatFrames;
    g.firstBeat = first;
    return g;
}

} // namespace ct
