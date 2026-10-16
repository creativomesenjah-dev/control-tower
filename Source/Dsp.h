// Dsp.h — bloques de DSP del preamp, sin dependencias de JUCE (se prueban offline).
//   SVF2        filtro de estado variable TPT (LP / BP / HP / allpass), estable al modular
//   Isolator    aislador de 2 a 5 bandas con crossovers Linkwitz-Riley 4º orden (suma plana)
//   DjFilter    filtro de un solo pote: izquierda pasa-bajos, derecha pasa-altos (CFX)
//   SpringReverb  reverb de resortes: cadena de allpass dispersivos dentro de un loop
//   MasterDynamics  drive + limitador de pico
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ct
{

constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

// ---------------------------------------------------------------------------
// Rampa lineal por bloque: barata y sin clicks.
struct Ramp
{
    float current = 1.0f, target = 1.0f;
    void snap (float v) { current = target = v; }
    void set (float v)  { target = v; }
    // Multiplica buf[0..n) por la rampa (avanza hasta target en este bloque).
    void apply (float* buf, int n)
    {
        if (current == target) { if (current != 1.0f) for (int i = 0; i < n; ++i) buf[i] *= current; return; }
        const float step = (target - current) / (float) n;
        float g = current;
        for (int i = 0; i < n; ++i) { g += step; buf[i] *= g; }
        current = target;
    }
    void applyAdd (const float* src, float* dst, int n)   // dst += src * rampa
    {
        if (current == target) { if (current != 0.0f) for (int i = 0; i < n; ++i) dst[i] += src[i] * current; return; }
        const float step = (target - current) / (float) n;
        float g = current;
        for (int i = 0; i < n; ++i) { g += step; dst[i] += src[i] * g; }
        current = target;
    }
};

// ---------------------------------------------------------------------------
struct SVF2
{
    float g = 0.0f, k = 1.41421356f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    void set (float hz, float sr, float q = 0.70710678f)
    {
        hz = std::clamp (hz, 10.0f, 0.45f * sr);
        g  = std::tan (kPi * hz / sr);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }

    inline float lowpass (float x)  { float l, b, h; tick (x, l, b, h); return l; }
    inline float highpass (float x) { float l, b, h; tick (x, l, b, h); return h; }
    inline float allpass (float x)  { float l, b, h; tick (x, l, b, h); return x - 2.0f * k * b; }
};

// ---------------------------------------------------------------------------
// Crossover Linkwitz-Riley 4º orden: low = LP2·LP2, high = HP2·HP2, low + high = allpass.
struct LR4Split
{
    SVF2 first, lo2, hi2;
    void set (float hz, float sr) { first.set (hz, sr); lo2.set (hz, sr); hi2.set (hz, sr); }
    void reset() { first.reset(); lo2.reset(); hi2.reset(); }
    inline void tick (float x, float& low, float& high)
    {
        float l, b, h;
        first.tick (x, l, b, h);
        low  = lo2.lowpass (l);
        high = hi2.highpass (h);
    }
};

// ---------------------------------------------------------------------------
// Aislador estéreo de 2..5 bandas. Con todas las bandas en 0 dB devuelve la señal con
// respuesta en amplitud plana (sólo cambia la fase, como cualquier crossover LR).
class Isolator
{
public:
    static constexpr int kMaxBands = 5;

    void prepare (float sampleRate) { sr = sampleRate; const auto f = freqs; configure (numBands, f.data()); }

    // freqs: numBands-1 frecuencias de corte ascendentes
    void configure (int bands, const float* crossovers)
    {
        numBands = std::clamp (bands, 2, kMaxBands);
        for (int i = 0; i < numBands - 1; ++i) freqs[(size_t) i] = crossovers[i];
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < numBands - 1; ++i)
            {
                splits[(size_t) ch][(size_t) i].set (freqs[(size_t) i], sr);
                splits[(size_t) ch][(size_t) i].reset();
            }
            // compensación: la banda baja del corte j pasa por los allpass de los cortes > j
            for (int j = 0; j < numBands - 1; ++j)
                for (int m = j + 1; m < numBands - 1; ++m)
                {
                    auto& ap = comp[(size_t) ch][(size_t) j][(size_t) m];
                    ap.set (freqs[(size_t) m], sr);
                    ap.reset();
                }
        }
        for (auto& g : gains) g.snap (g.target);
    }

    int getNumBands() const { return numBands; }

    // Ganancia lineal por banda (0 = kill). Se interpola durante el bloque.
    void setBandGain (int band, float linear) { if (band >= 0 && band < kMaxBands) gains[(size_t) band].set (linear); }

    void process (float* L, float* R, int n)
    {
        float g[kMaxBands], step[kMaxBands];
        for (int b = 0; b < numBands; ++b)
        {
            g[b] = gains[(size_t) b].current;
            step[b] = (gains[(size_t) b].target - g[b]) / (float) n;
            gains[(size_t) b].current = gains[(size_t) b].target;
        }

        float* chans[2] = { L, R };
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = chans[ch];
            if (x == nullptr) continue;
            auto& sp = splits[(size_t) ch];
            auto& cp = comp[(size_t) ch];
            for (int i = 0; i < n; ++i)
            {
                // árbol en cascada desde abajo: el resto "alto" se sigue partiendo.
                // Cada banda baja j pasa por los allpass de los cortes superiores (m > j),
                // así todas quedan con la misma fase y la suma es plana.
                float rest = x[i], y = 0.0f;
                for (int j = 0; j < numBands - 1; ++j)
                {
                    float low, high;
                    sp[(size_t) j].tick (rest, low, high);
                    for (int m = j + 1; m < numBands - 1; ++m)
                        low = cp[(size_t) j][(size_t) m].allpass (low);
                    y += low * (g[j] + step[j] * (float) i);
                    rest = high;
                }
                y += rest * (g[numBands - 1] + step[numBands - 1] * (float) i);
                x[i] = y;
            }
        }
    }

private:
    float sr = 48000.0f;
    int numBands = 4;
    std::array<float, kMaxBands - 1> freqs { 100.0f, 400.0f, 2500.0f, 8000.0f };
    std::array<std::array<LR4Split, kMaxBands - 1>, 2> splits;
    std::array<std::array<std::array<SVF2, kMaxBands - 1>, kMaxBands - 1>, 2> comp;
    std::array<Ramp, kMaxBands> gains;
};

// ---------------------------------------------------------------------------
// Filtro DJ de un pote (como el CFX de la FLX4): 0..0.5 pasa-bajos, 0.5..1 pasa-altos.
class DjFilter
{
public:
    void prepare (float sampleRate) { sr = sampleRate; for (auto& f : f2) f.reset(); }

    void setPosition (float pos01, float resonance01)
    {
        pos = std::clamp (pos01, 0.0f, 1.0f);
        q = 0.707f + 4.5f * std::clamp (resonance01, 0.0f, 1.0f);
    }

    void process (float* L, float* R, int n)
    {
        const float d = pos - 0.5f;
        const bool active = std::abs (d) > 0.02f;
        // mezcla suave al entrar / salir de la zona muerta
        const float wetTarget = active ? 1.0f : 0.0f;
        if (! active && wet <= 0.0f) return;

        float hz;
        if (d < 0.0f) { lowpassMode = true;  hz = 20000.0f * std::pow (60.0f / 20000.0f, std::min (1.0f, (-d - 0.02f) / 0.48f)); }
        else if (d > 0.0f) { lowpassMode = false; hz = 20.0f * std::pow (9000.0f / 20.0f, std::min (1.0f, (d - 0.02f) / 0.48f)); }
        else hz = lowpassMode ? 20000.0f : 20.0f;

        // cutoff suavizado (log) para barridos sin escalones
        if (curHz <= 0.0f) curHz = hz;
        const float startHz = curHz;
        curHz = hz;
        const int chunks = (n + 31) / 32;
        float* ch[2] = { L, R };
        const float wStart = wet, wStep = (wetTarget - wet) / (float) n;
        for (int c = 0; c < chunks; ++c)
        {
            const int a = c * 32, b = std::min (n, a + 32);
            const float t = (float) (c + 1) / (float) chunks;
            const float h = startHz * std::pow (hz / startHz, t);
            for (int k = 0; k < 2; ++k) f2[(size_t) k].set (h, sr, q);
            for (int k = 0; k < 2; ++k)
            {
                if (ch[k] == nullptr) continue;
                for (int i = a; i < b; ++i)
                {
                    float lp, bp, hp;
                    f2[(size_t) k].tick (ch[k][i], lp, bp, hp);
                    const float y = lowpassMode ? lp : hp;
                    const float w = wStart + wStep * (float) i;
                    ch[k][i] += w * (y - ch[k][i]);
                }
            }
        }
        wet = wetTarget;
        if (wet <= 0.0f) { for (auto& f : f2) f.reset(); curHz = -1.0f; }
    }

private:
    float sr = 48000.0f, pos = 0.5f, q = 1.0f, wet = 0.0f, curHz = -1.0f;
    bool lowpassMode = true;
    std::array<SVF2, 2> f2;
};

// ---------------------------------------------------------------------------
// Reverb de resortes. Cada "resorte" es un loop: retardo modulado -> 8 allpass de 1er orden
// en cascada (la dispersión que da el "boing" metálico) -> amortiguación -> realimentación.
class SpringReverb
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& t : tanks) { t.buf.assign ((size_t) (0.12f * sr) + 8, 0.0f); t.w = 0; t.state = 0.0f; t.ap.fill ({}); t.damp = 0.0f; }
        inHp.reset(); inLp.reset(); outHp[0].reset(); outHp[1].reset();
        inHp.set (180.0f, sr); inLp.set (5200.0f, sr);
        outHp[0].set (150.0f, sr); outHp[1].set (150.0f, sr);
        setParams (0.5f, 0.5f, 0.5f);
    }

    // size: largo de los resortes; decay: cola; tone: brillo
    void setParams (float size01, float decay01, float tone01)
    {
        const float base = 0.028f + 0.045f * std::clamp (size01, 0.0f, 1.0f);
        tanks[0].lenSmp = base * sr;
        tanks[1].lenSmp = base * 1.37f * sr;
        fb = 0.55f + 0.38f * std::clamp (decay01, 0.0f, 1.0f);
        apCoef = 0.62f + 0.12f * std::clamp (size01, 0.0f, 1.0f);
        dampCoef = std::exp (-2.0f * kPi * (1500.0f + 6000.0f * std::clamp (tone01, 0.0f, 1.0f)) / sr);
    }

    // in: mono. Suma la reverb (estéreo) en outL/outR con ganancia gain.
    void process (const float* in, float* outL, float* outR, int n, float gain)
    {
        for (int i = 0; i < n; ++i)
        {
            float x = inLp.lowpass (inHp.highpass (in[i]));
            modPhase += 0.6f / sr; if (modPhase >= 1.0f) modPhase -= 1.0f;
            const float mod = std::sin (2.0f * kPi * modPhase);
            float o[2];
            for (int t = 0; t < 2; ++t)
            {
                auto& tk = tanks[(size_t) t];
                // lectura del retardo (lineal, modulación leve)
                const float d = tk.lenSmp * (1.0f + 0.004f * (t == 0 ? mod : -mod));
                const size_t size = tk.buf.size();
                float rp = (float) tk.w - d;
                while (rp < 0.0f) rp += (float) size;
                const size_t i0 = (size_t) rp % size, i1 = (i0 + 1) % size;
                const float fr = rp - std::floor (rp);
                float y = tk.buf[i0] + fr * (tk.buf[i1] - tk.buf[i0]);
                // dispersión
                for (auto& s : tk.ap)
                {
                    const float v = -apCoef * y + s;
                    s = y + apCoef * v;
                    y = v;
                }
                // amortiguación
                tk.damp = y + dampCoef * (tk.damp - y);
                y = tk.damp;
                tk.buf[tk.w] = std::tanh (x + fb * y);
                tk.w = (tk.w + 1) % size;
                o[t] = y;
            }
            outL[i] += gain * outHp[0].highpass (o[0]);
            outR[i] += gain * outHp[1].highpass (o[1]);
        }
    }

private:
    struct Tank
    {
        std::vector<float> buf;
        size_t w = 0;
        float lenSmp = 1000.0f, state = 0.0f, damp = 0.0f;
        std::array<float, 8> ap {};
    };
    std::array<Tank, 2> tanks;
    SVF2 inHp, inLp;
    std::array<SVF2, 2> outHp;
    float sr = 48000.0f, fb = 0.7f, apCoef = 0.68f, dampCoef = 0.5f, modPhase = 0.0f;
};

// ---------------------------------------------------------------------------
// Drive (saturación suave) + limitador de pico. Estéreo, en el lugar.
class MasterDynamics
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        attack = std::exp (-1.0f / (0.0005f * sr));
        release = std::exp (-1.0f / (0.12f * sr));
        env = 0.0f;
    }

    void setDrive (float d01)       { drive = std::clamp (d01, 0.0f, 1.0f); }
    void setCeilingDb (float db)    { ceiling = dbToGain (db); }
    void setLimiterOn (bool on)     { limiterOn = on; }
    float getGainReductionDb() const { return grDb; }

    void process (float* L, float* R, int n)
    {
        // saturación: con drive el nivel típico (picos de 0,5) queda igual, los picos se redondean
        const float g = 1.0f + 7.0f * drive * drive;
        const float makeup = 0.5f / std::tanh (0.5f * g);
        float minGain = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R != nullptr ? R[i] : l;
            if (drive > 0.001f)
            {
                l = std::tanh (g * l) * makeup;
                r = std::tanh (g * r) * makeup;
            }
            if (limiterOn)
            {
                const float pk = std::max (std::abs (l), std::abs (r));
                env = pk > env ? pk + attack * (env - pk) : pk + release * (env - pk);
                const float gr = env > ceiling ? ceiling / env : 1.0f;
                minGain = std::min (minGain, gr);
                l *= gr; r *= gr;
                // seguridad: nunca pasar el techo
                l = std::clamp (l, -1.0f, 1.0f);
                r = std::clamp (r, -1.0f, 1.0f);
            }
            L[i] = l;
            if (R != nullptr) R[i] = r;
        }
        grDb = gainToDb (minGain);
    }

private:
    float sr = 48000.0f, drive = 0.0f, ceiling = 0.97f, attack = 0.0f, release = 0.0f, env = 0.0f, grDb = 0.0f;
    bool limiterOn = true;
};

// ---------------------------------------------------------------------------
// Estante de graves (SUB BOOST): +dB debajo de ~70 Hz, como un low-shelf de 2º orden.
class SubBoost
{
public:
    void prepare (float sampleRate) { sr = sampleRate; for (auto& f : lp) f.set (75.0f, sr, 0.6f); amount.snap (0.0f); }
    void setOn (bool on, float db = 6.0f) { amount.set (on ? dbToGain (db) - 1.0f : 0.0f); }
    void process (float* L, float* R, int n)
    {
        if (amount.current == 0.0f && amount.target == 0.0f) { for (auto& f : lp) f.reset(); return; }
        const float a0 = amount.current, step = (amount.target - a0) / (float) n;
        float* ch[2] = { L, R };
        for (int k = 0; k < 2; ++k)
            if (ch[k] != nullptr)
                for (int i = 0; i < n; ++i)
                    ch[k][i] += (a0 + step * (float) i) * lp[(size_t) k].lowpass (ch[k][i]);
        amount.current = amount.target;
    }
private:
    float sr = 48000.0f;
    std::array<SVF2, 2> lp;
    Ramp amount;
};

} // namespace ct
