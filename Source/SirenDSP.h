// SirenDSP.h — sirena de dub analógica al estilo Benidub DS01 (cuadrada + LFO + RC), calibrada
// contra una grabación de referencia. Sin dependencias de JUCE.
//
// Panel como el del aparato:
//   TONO (4):   SENO 1 · SENO 2 · TEST · CUADRADA          -> forma y profundidad del LFO
//   PITCH (3):  GRAVE · MEDIA · AGUDA                       -> frecuencia base del oscilador
//   LFO (3):    LENTO · MEDIO · RÁPIDO                      -> velocidad del LFO
//   MANUAL:     disparo sin LFO: mientras lo mantenés el tono sube (carga de capacitor), al soltar baja
//
// Medido en la referencia: onda cuadrada (sólo armónicos impares) con un filtro cálido fijo
// (3º armónico a -12 dB, 7º a -31 dB con la base en 570 Hz); el pitch se mueve en semitonos
// (VCO exponencial); al soltar el tono cae ~3,5 semitonos en 0,25 s y el volumen se apaga en ~80 ms.
#pragma once

#include <algorithm>
#include <cmath>

namespace ct
{

constexpr int kNumSirenTones   = 4;
constexpr int kSirenManual     = 4;               // disparo 5 = MANUAL
constexpr int kNumSirenPresets = 5;               // 4 tonos + MANUAL (lo que se puede disparar)
constexpr int kNumSirenPitches = 3;
constexpr int kNumSirenSpeeds  = 3;

struct SirenTone
{
    enum Lfo { Sine = 0, ClippedTriangle, Square };
    const char* name;
    Lfo   lfo;
    float lfoHz;       // velocidad en la posición MEDIO
    float depthSemis;  // excursión total del LFO
};

inline const SirenTone& sirenTone (int i)
{
    static const SirenTone tones[kNumSirenTones] = {
        { "SENO 1",   SirenTone::Sine,            2.2f,  3.0f },   // ondulante suave (±1,5 st, 2,2 Hz)
        { "SENO 2",   SirenTone::ClippedTriangle, 3.2f, 12.6f },   // triángulo de una octava con mesetas
        { "TEST",     SirenTone::Square,          6.0f,  7.0f },   // dos tonos rápido
        { "CUADRADA", SirenTone::Square,          2.4f,  5.0f },   // dos tonos lento
    };
    return tones[std::clamp (i, 0, kNumSirenTones - 1)];
}

inline const char* sirenTriggerName (int i)
{
    return i == kSirenManual ? "MANUAL" : sirenTone (i).name;
}

inline float sirenPitchHz (int pos)
{
    static const float hz[kNumSirenPitches] = { 210.0f, 570.0f, 1250.0f };   // medidos en la referencia
    return hz[std::clamp (pos, 0, kNumSirenPitches - 1)];
}
inline const char* sirenPitchName (int pos)
{
    static const char* n[kNumSirenPitches] = { "GRAVE", "MEDIA", "AGUDA" };
    return n[std::clamp (pos, 0, kNumSirenPitches - 1)];
}
inline float sirenSpeedMul (int s)
{
    static const float m[kNumSirenSpeeds] = { 0.45f, 1.0f, 2.1f };
    return m[std::clamp (s, 0, kNumSirenSpeeds - 1)];
}
inline const char* sirenSpeedName (int s)
{
    static const char* n[kNumSirenSpeeds] = { "LENTO", "MEDIO", "RÁPIDO" };
    return n[std::clamp (s, 0, kNumSirenSpeeds - 1)];
}

class Siren
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        lp1.set (1500.0f, sr);
        lp2.set (4200.0f, sr);
        reset();
    }

    void reset()
    {
        phase = lfoPhase = 0.0f;
        amp = 0.0f;
        held = active = false;
        releaseTime = 0.0f;
        manualEnv = 0.0f;
        dropSemis = 0.0f;
        logFreq = std::log2 (570.0f);
        lfoRc = 0.0f;
        lp1.z = lp2.z = 0.0f;
        dcX = dcY = 0.0f;
        drift = driftTarget = 0.0f;
    }

    void setPanel (int pitchPos, int speedPos) { pitchSel = pitchPos; speedSel = speedPos; }

    // trigger 0..3 = tonos, 4 = MANUAL
    void noteOn (int trigger)
    {
        const bool wasSilent = ! active;
        current = std::clamp (trigger, 0, kNumSirenPresets - 1);
        held = active = true;
        releaseTime = 0.0f;
        dropSemis = 0.0f;
        if (current != kSirenManual) { lfoPhase = 0.0f; lastTone = current; }
        if (wasSilent) { logFreq = std::log2 (sirenPitchHz (pitchSel)); manualEnv = 0.0f; lfoRc = 0.0f; }
    }

    void noteOff() { if (held) { held = false; releaseTime = 0.0f; } }

    // Ajuste fino desde el pad XY / las perillas: X = ±1 octava sobre el pitch elegido, Y = velocidad x0,25..x4
    void setXY (float x01, float y01)
    {
        if (x01 >= 0.0f) pitchMul = std::exp2 ((std::clamp (x01, 0.0f, 1.0f) - 0.5f) * 2.0f);
        if (y01 >= 0.0f) rateMul  = std::exp2 ((std::clamp (y01, 0.0f, 1.0f) - 0.5f) * 4.0f);
    }

    bool isHeld() const   { return held; }
    bool isActive() const { return active; }
    int  getPreset() const { return current; }

    bool process (float* out, int n, float level)
    {
        if (! active) return false;
        const float dt = 1.0f / sr;
        const bool manual = current == kSirenManual;
        const auto& t = sirenTone (lastTone);
        const float attackStep = dt / 0.004f;
        const float glide = 1.0f - std::exp (-dt / 0.004f);
        const float rcCoef = 1.0f - std::exp (-dt / 0.010f);
        const float dcCoef = 1.0f - 2.0f * 3.14159265f * 40.0f * dt;
        // MANUAL: carga y descarga RC (medido: baja ~9,5 st en 0,6 s; sube algo más rápido)
        const float chargeCoef = 1.0f - std::exp (-dt / (0.30f / rateMul));
        const float dischargeCoef = 1.0f - std::exp (-dt / (0.50f / rateMul));
        // soltar: el pitch cae 3,5 st (tau 0,12 s); volumen tau 35 ms (tonos) o 0,45 s (MANUAL, deja oír la bajada)
        const float dropCoef = 1.0f - std::exp (-dt / 0.12f);
        const float ampRelease = std::exp (-dt / (manual ? 0.45f : 0.035f));
        const float lfoHz = t.lfoHz * sirenSpeedMul (speedSel) * rateMul;
        const float baseLog = std::log2 (sirenPitchHz (pitchSel) * pitchMul);

        for (int i = 0; i < n; ++i)
        {
            if (held) amp = std::min (1.0f, amp + attackStep);
            else
            {
                releaseTime += dt;
                if (! manual) dropSemis += (-3.5f - dropSemis) * dropCoef;
                amp *= ampRelease;
                if (amp < 0.0005f) { active = false; amp = 0.0f; break; }
            }

            float semis;
            if (manual)
            {
                // el botón carga un capacitor: el tono sube hasta ~12 st y al soltar vuelve a bajar
                manualEnv += ((held ? 1.0f : 0.0f) - manualEnv) * (held ? chargeCoef : dischargeCoef);
                semis = 12.0f * manualEnv - 2.0f * (1.0f - manualEnv) * std::min (releaseTime * 2.0f, 1.0f);
            }
            else
            {
                lfoPhase += lfoHz * dt;
                lfoPhase -= std::floor (lfoPhase);
                float lfo;   // 0..1
                switch (t.lfo)
                {
                    case SirenTone::Sine:
                        lfo = 0.5f - 0.5f * std::cos (6.2831853f * lfoPhase);
                        break;
                    case SirenTone::ClippedTriangle:
                    {
                        // triángulo que se recorta: queda un instante quieto arriba y abajo (medido)
                        const float tri = 1.0f - std::abs (2.0f * lfoPhase - 1.0f);
                        lfo = std::clamp ((tri - 0.5f) * 1.25f + 0.5f, 0.0f, 1.0f);
                        break;
                    }
                    case SirenTone::Square:
                    default:
                        lfo = lfoPhase < 0.5f ? 0.0f : 1.0f;
                        break;
                }
                lfoRc += (lfo - lfoRc) * rcCoef;     // el LFO pasa por una red RC
                semis = (lfoRc - 0.5f) * t.depthSemis;
            }

            if (--driftCount <= 0) { driftCount = (int) (sr * 0.35f); driftTarget = rnd() * 0.06f; }
            drift += (driftTarget - drift) * (2.0f * dt);

            const float targetLog = baseLog + (semis + dropSemis + drift) / 12.0f;
            logFreq += (targetLog - logFreq) * glide;
            const float freq = std::clamp (std::exp2 (logFreq), 20.0f, 0.42f * sr);
            const float inc = freq * dt;

            // cuadrada simétrica (sólo armónicos impares, como en la referencia) con polyBLEP
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            float ph2 = phase + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
            const float s = (phase < 0.5f ? 1.0f : -1.0f) + polyBlep (phase, inc) - polyBlep (ph2, inc);

            // filtro cálido fijo (dos polos simples, 1,5 y 4,2 kHz), un poco de saturación, acople sin DC
            const float v = lp2.tick (lp1.tick (s));
            const float sat = std::tanh (1.3f * v) * 0.36f;
            const float dc = sat - dcX + dcCoef * dcY;
            dcX = sat; dcY = dc;
            out[i] += dc * amp * level;
        }
        return true;
    }

private:
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void set (float hz, float s) { a = 1.0f - std::exp (-2.0f * 3.14159265f * hz / s); }
        inline float tick (float x) { z += a * (x - z); return z; }
    };

    static float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 8388608.0f - 1.0f; }

    float sr = 48000.0f;
    OnePole lp1, lp2;
    float phase = 0.0f, lfoPhase = 0.0f, amp = 0.0f, releaseTime = 0.0f;
    float manualEnv = 0.0f, dropSemis = 0.0f, logFreq = 9.15f, lfoRc = 0.0f;
    float dcX = 0.0f, dcY = 0.0f, drift = 0.0f, driftTarget = 0.0f;
    float pitchMul = 1.0f, rateMul = 1.0f;
    int driftCount = 0, current = 0, lastTone = 0, pitchSel = 1, speedSel = 1;
    unsigned rng = 4242u;
    bool held = false, active = false;
};

} // namespace ct
