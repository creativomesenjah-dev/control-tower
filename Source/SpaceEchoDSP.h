// SpaceEchoDSP.h — eco de cinta de 3 cabezales al estilo de los ecos de cinta clásicos de dub
// (tipo Roland RE-201). Sin dependencias de JUCE.
//
//   entrada ─► pre-énfasis ─► saturación de cinta ─► CINTA ─┬─ cabezal 1 (1×) ─┐
//              ▲                                            ├─ cabezal 2 (2×) ─┼─► suma de cabezales elegidos
//              │                                            └─ cabezal 3 (3×) ─┘          │
//              │                                                                          ▼
//              └──── intensidad ◄── filtros del loop ◄── tono ◄── golpe de cabezal + pérdida de agudos
//
// - TIEMPO = cabezal 1; los otros dos están a ~1,96× y ~2,92× (como la velocidad de cinta es una sola,
//   cambiarla dobla el pitch de los tres a la vez).
// - Los agudos de la cinta dependen de la velocidad: cinta lenta (tiempo largo) = repeticiones más oscuras.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ct
{

struct SpaceEchoParams
{
    float timeMs      = 375.0f;  // cabezal 1: 20 .. 2000
    int   heads       = 3;       // máscara de cabezales: bit0 = 1, bit1 = 2, bit2 = 3
    float intensity   = 0.55f;   // 0 .. 1.1 (más de 1: autooscilación contenida por la cinta)
    float inertiaSec  = 0.35f;
    float wowFlutter  = 0.25f;   // 0 .. 1
    float drive       = 0.35f;   // 0 .. 1
    float tone        = 0.0f;    // -1 oscuro .. +1 brillante
    float lowCutHz    = 120.0f;
    float highCutHz   = 6000.0f;
    float outputGain  = 1.0f;
    bool  freeze      = false;
};

// Modos del selector de cabezales (como el MODE de los ecos de cinta)
constexpr int kNumHeadModes = 7;
inline int headModeMask (int mode)
{
    static const int m[kNumHeadModes] = { 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111 };
    return m[std::clamp (mode, 0, kNumHeadModes - 1)];
}
inline const char* headModeLabel (int mode)
{
    static const char* l[kNumHeadModes] = { "1", "2", "3", "1+2", "2+3", "1+3", "1+2+3" };
    return l[std::clamp (mode, 0, kNumHeadModes - 1)];
}

class SpaceEcho
{
public:
    static constexpr float kMinTimeMs = 20.0f;
    static constexpr float kMaxTimeMs = 2000.0f;
    static constexpr float kMaxIntensity = 1.10f;
    // separación real de los cabezales de reproducción respecto del de grabación (no es 1:2:3 exacto)
    static constexpr float kHeadRatio[3] = { 1.0f, 1.96f, 2.92f };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const auto maxSamples = (size_t) std::ceil ((kMaxTimeMs * 2.92f * 1.01f + 60.0f) * 0.001f * sr) + 16;
        size_t size = 1;
        while (size < maxSamples) size <<= 1;
        tape.assign (size, 0.0f);
        mask = size - 1;
        w = 0;
        delay = params.timeMs * 0.001f * sr;
        reset();
    }

    void reset()
    {
        std::fill (tape.begin(), tape.end(), 0.0f);
        bump.reset(); hfLoss.reset(); hp.reset(); lp.reset(); toneSplit = 0.0f;
        preEmph = deEmph = 0.0f;
        dcX = dcY = 0.0f;
        killing = false; killGain = 1.0f;
        for (auto& g : headGain) g = 0.0f;
        fbSmooth = params.intensity;
        noiseState = 0.0f;
    }

    void setParams (const SpaceEchoParams& p) { params = p; }
    void triggerKill() { killing = true; }
    float getCurrentDelayMs() const { return delay / sr * 1000.0f; }

    // in: mono. out: mono, sólo eco (100% wet).
    void process (const float* in, float* out, int n)
    {
        const float target = std::clamp (params.timeMs, kMinTimeMs, kMaxTimeMs) * 0.001f * sr;
        const float glideCoef = std::exp (-1.0f / (std::max (params.inertiaSec, 0.005f) * sr));

        // filtros dependientes de la velocidad de cinta (se recalculan por bloque)
        const float ms = delay / sr * 1000.0f;
        const float tapeHf = std::clamp (6800.0f * std::pow (300.0f / std::max (ms, 20.0f), 0.35f), 1800.0f, 11000.0f);
        hfLoss.set (tapeHf, sr, 0.6f);
        bump.set (95.0f, sr, 1.1f);
        hp.set (params.lowCutHz, sr, 0.6f);
        lp.set (params.highCutHz, sr, 0.6f);
        const float toneCoef = std::exp (-2.0f * 3.14159265f * 900.0f / sr);
        const float t = std::clamp (params.tone, -1.0f, 1.0f);
        const float gLow = std::pow (10.0f, -t * 5.0f / 20.0f), gHigh = std::pow (10.0f, t * 9.0f / 20.0f);
        const float emphCoef = std::exp (-2.0f * 3.14159265f * 2500.0f / sr);

        const float wf = std::clamp (params.wowFlutter, 0.0f, 1.0f);
        const float wowDepth = wf * 0.0028f * sr, flutDepth = wf * 0.00010f * sr;
        const float drive = std::clamp (params.drive, 0.0f, 1.0f);
        const float satG = 0.7f + 3.0f * drive * std::sqrt (drive);
        const float asym = 0.10f * drive;

        // cabezales activos y ganancias (suavizadas para cambiar de modo sin clicks)
        const int hm = params.freeze ? (params.heads == 0 ? 1 : params.heads) : params.heads;
        int nActive = 0;
        for (int k = 0; k < 3; ++k) if (hm & (1 << k)) ++nActive;
        const float headTarget = nActive > 0 ? 1.0f / std::sqrt ((float) nActive) : 0.0f;
        // la suma de cabezales ya está escalada por 1/√n; con 1/√n más en la realimentación la ganancia
        // del loop en fase queda igual a la intensidad en cualquier modo (no se dispara al cambiar de modo)
        const float fbNorm = 1.0f / std::sqrt ((float) std::max (1, nActive));
        // makeup: compensa las pérdidas de la cinta y los filtros, así cerca del 90-100 % empieza a
        // autooscilar como un eco de cinta real (y no recién al máximo)
        const float fbTarget = params.freeze ? fbNorm : std::clamp (params.intensity, 0.0f, kMaxIntensity) * fbNorm * 1.15f;
        const float headStep = 1.0f / (0.01f * sr);
        const float killStep = 1.0f / (0.02f * sr);
        const float dcCoef = 1.0f - (2.0f * 3.14159265f * 10.0f / sr);

        for (int i = 0; i < n; ++i)
        {
            // velocidad de cinta con inercia
            float step = (target - delay) * (1.0f - glideCoef);
            step = std::clamp (step, -0.85f, 0.85f);
            delay += step;

            // wow & flutter compartidos (una sola cinta)
            wowPhase += 0.6f / sr;   if (wowPhase >= 1.0f) wowPhase -= 1.0f;
            flutPhase += 7.1f / sr;  if (flutPhase >= 1.0f) flutPhase -= 1.0f;
            if (--noiseCounter <= 0) { noiseCounter = (int) (sr * 0.2f); noiseTarget = rnd(); }
            noiseState += (noiseTarget - noiseState) * (5.0f / sr);
            const float mod = wowDepth * (0.5f + 0.5f * std::sin (6.2831853f * wowPhase) + 0.4f * noiseState)
                            + flutDepth * std::sin (6.2831853f * flutPhase);

            // lectura de los 3 cabezales
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float tg = (hm & (1 << k)) ? headTarget : 0.0f;
                headGain[k] += std::clamp (tg - headGain[k], -headStep, headStep);
                if (headGain[k] <= 0.0f) continue;
                const float d = std::max ((delay + mod) * kHeadRatio[k], 4.0f);
                sum += headGain[k] * readHermite (d);
            }

            // carácter de la cinta: golpe de cabezal (graves) y pérdida de agudos
            float bl, bb, bh;
            bump.tick (sum, bl, bb, bh);
            float y = sum + 0.35f * bb;
            y = hfLoss.lowpass (y);
            // de-énfasis (compensa el pre-énfasis de la grabación)
            deEmph = y + emphCoef * (deEmph - y);
            y = 0.65f * y + 0.35f * deEmph;   // la referencia conserva bastante agudo por repetición

            // tono (inclinación grave / agudo) y filtros del loop
            toneSplit = y + toneCoef * (toneSplit - y);
            y = gLow * toneSplit + gHigh * (y - toneSplit);
            if (! params.freeze)
            {
                float l1, b1, h1;  hp.tick (y, l1, b1, h1);
                float l2, b2, h2;  lp.tick (h1, l2, b2, h2);
                y = l2;
            }

            // kill
            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f)
                {
                    std::fill (tape.begin(), tape.end(), 0.0f);
                    bump.reset(); hfLoss.reset(); hp.reset(); lp.reset();
                    killing = false; killGain = 1.0f;
                    y = 0.0f;
                }
            }
            const float kg = killing ? killGain : 1.0f;

            // grabación: entrada + realimentación, con pre-énfasis y saturación
            fbSmooth += (fbTarget - fbSmooth) * 0.0015f;
            const float x = params.freeze ? 0.0f : in[i];
            float rec = x + fbSmooth * y * kg;
            const float hiPart = rec - (preEmph = rec + emphCoef * (preEmph - rec));
            rec = rec + 0.6f * hiPart;                         // pre-énfasis: los agudos saturan primero
            rec = rec + asym * rec * rec;
            rec = std::tanh (satG * rec) / satG;
            rec += 0.00003f * rnd() * wf;                      // un poco de soplido de cinta
            if (! std::isfinite (rec)) rec = 0.0f;
            tape[w & mask] = rec;
            ++w;

            // salida sin DC, con techo suave
            float o = y * params.outputGain * kg;
            const float dc = o - dcX + dcCoef * dcY;
            dcX = o; dcY = dc;
            out[i] = std::tanh (dc);
        }
    }

private:
    struct SVF
    {
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1.4142f;
        void reset() { ic1 = ic2 = 0; }
        void set (float hz, float s, float q)
        {
            hz = std::clamp (hz, 10.0f, 0.45f * s);
            const float g = std::tan (3.14159265f * hz / s);
            k = 1.0f / q; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        inline void tick (float x, float& l, float& b, float& h)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; l = v2; b = v1; h = x - k * v1 - v2;
        }
        inline float lowpass (float x) { float l, b, h; tick (x, l, b, h); return l; }
    };

    inline float readHermite (float d) const
    {
        const double rp = (double) (int64_t) (w & mask) - (double) d;
        const double fl = std::floor (rp);
        const float t = (float) (rp - fl);
        const auto i = (int64_t) fl;
        const float ym1 = tape[(size_t) (i - 1) & mask], y0 = tape[(size_t) i & mask];
        const float y1 = tape[(size_t) (i + 1) & mask], y2 = tape[(size_t) (i + 2) & mask];
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 8388608.0f - 1.0f; }

    SpaceEchoParams params;
    float sr = 48000.0f;
    std::vector<float> tape;
    size_t mask = 0;
    uint64_t w = 0;
    float delay = 0.0f;
    float headGain[3] {};
    float fbSmooth = 0.5f;
    SVF bump, hfLoss, hp, lp;
    float toneSplit = 0.0f, preEmph = 0.0f, deEmph = 0.0f;
    float wowPhase = 0.0f, flutPhase = 0.0f, noiseState = 0.0f, noiseTarget = 0.0f;
    int noiseCounter = 0;
    uint32_t rng = 777u;
    float dcX = 0.0f, dcY = 0.0f, killGain = 1.0f;
    bool killing = false;
};

} // namespace ct
