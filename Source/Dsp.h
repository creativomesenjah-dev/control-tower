// Dsp.h — bloques de DSP del preamp, sin dependencias de JUCE (se prueban offline).
//   SVF2        filtro de estado variable TPT (LP / BP / HP / allpass), estable al modular
//   Isolator    aislador de 2 a 5 bandas con crossovers Linkwitz-Riley 4º orden (suma plana)
//   Analog24    filtro analógico 24 dB con resonancia y saturación (núcleo del CFX y del master)
//   DjFilter    filtro de un solo pote: izquierda pasa-bajos, derecha pasa-altos (CFX)
//   HpLpFilter  pasa-altos + pasa-bajos del master
//   SpringReverb  resortes físicos: allpass estirados dispersivos dentro de 3 loops
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
// tanh rápida (racional, error < 0,3 % hasta |x| = 3, satura suave después)
inline float fastTanh (float x)
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Filtro analógico de 24 dB/oct (dos etapas de estado variable en cascada) con saturación:
// el integrador de la etapa resonante se limita suave, así la resonancia "canta" sin pitidos
// digitales y se comprime con nivel alto, como un filtro a transistores. Con señal normal es limpio.
struct Analog24
{
    SVF2 s1, s2;
    bool highpassMode = false;

    void reset() { s1.reset(); s2.reset(); }
    void set (float hz, float sr, float res01, bool hp)
    {
        highpassMode = hp;
        s1.set (hz, sr, 0.54f);
        s2.set (hz, sr, 0.62f + 7.0f * res01 * res01);   // Q hasta ~7.6
    }
    inline float tick (float x)
    {
        float l, b, h;
        s1.tick (x, l, b, h);
        const float y1 = highpassMode ? h : l;
        s2.tick (y1, l, b, h);
        s2.ic1 = 2.5f * fastTanh (s2.ic1 * 0.4f);           // el "capacitor" de la etapa resonante satura
        return highpassMode ? h : l;
    }
};

// ---------------------------------------------------------------------------
// Filtro DJ de un pote (como el CFX de la FLX4): 0..0.5 pasa-bajos, 0.5..1 pasa-altos. 24 dB, analógico.
class DjFilter
{
public:
    void prepare (float sampleRate) { sr = sampleRate; for (auto& f : f2) f.reset(); }

    void setPosition (float pos01, float resonance01)
    {
        pos = std::clamp (pos01, 0.0f, 1.0f);
        res = std::clamp (resonance01, 0.0f, 1.0f);
    }

    void process (float* L, float* R, int n)
    {
        const float d = pos - 0.5f;
        const bool active = std::abs (d) > 0.02f;
        const float wetTarget = active ? 1.0f : 0.0f;
        if (! active && wet <= 0.0f) return;

        float hz;
        if (d < 0.0f) { lowpassMode = true;  hz = 20000.0f * std::pow (70.0f / 20000.0f, std::min (1.0f, (-d - 0.02f) / 0.48f)); }
        else if (d > 0.0f) { lowpassMode = false; hz = 20.0f * std::pow (8000.0f / 20.0f, std::min (1.0f, (d - 0.02f) / 0.48f)); }
        else hz = lowpassMode ? 20000.0f : 20.0f;

        if (curHz <= 0.0f) curHz = hz;
        const float startHz = curHz;
        curHz = hz;
        const int chunks = (n + 15) / 16;
        float* ch[2] = { L, R };
        const float wStart = wet, wStep = (wetTarget - wet) / (float) n;
        for (int c = 0; c < chunks; ++c)
        {
            const int a = c * 16, b = std::min (n, a + 16);
            const float t = (float) (c + 1) / (float) chunks;
            const float h = startHz * std::pow (hz / startHz, t);
            for (int k = 0; k < 2; ++k) f2[(size_t) k].set (h, sr, res, ! lowpassMode);
            for (int k = 0; k < 2; ++k)
            {
                if (ch[k] == nullptr) continue;
                for (int i = a; i < b; ++i)
                {
                    const float y = f2[(size_t) k].tick (ch[k][i]);
                    const float w = wStart + wStep * (float) i;
                    ch[k][i] += w * (y - ch[k][i]);
                }
            }
        }
        wet = wetTarget;
        if (wet <= 0.0f) { for (auto& f : f2) f.reset(); curHz = -1.0f; }
    }

private:
    float sr = 48000.0f, pos = 0.5f, res = 0.35f, wet = 0.0f, curHz = -1.0f;
    bool lowpassMode = true;
    std::array<Analog24, 2> f2;
};

// ---------------------------------------------------------------------------
// Master: pasa-altos y pasa-bajos separados (24 dB, analógicos, con resonancia), como en un mixer de dub.
// HP en 20 Hz y LP en 20 kHz = fuera del camino (bypass sin costo).
class HpLpFilter
{
public:
    void prepare (float sampleRate) { sr = sampleRate; for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); hpCur = 20.0f; lpCur = 20000.0f; }

    void setParams (float hpHz, float lpHz, float res01)
    {
        hpHz_ = std::clamp (hpHz, 20.0f, 4000.0f);
        lpHz_ = std::clamp (lpHz, 100.0f, 20000.0f);
        res = std::clamp (res01, 0.0f, 1.0f);
    }

    void process (float* L, float* R, int n)
    {
        process (hp, hpCur, hpHz_, hpOn, true,  L, R, n);
        process (lp, lpCur, lpHz_, lpOn, false, L, R, n);
    }

private:
    void process (std::array<Analog24, 2>& f, float& cur, float target, float& wet, bool isHp, float* L, float* R, int n)
    {
        const bool active = isHp ? target > 21.0f : target < 19800.0f;
        const float wetTarget = active ? 1.0f : 0.0f;
        if (! active && wet <= 0.0f) { cur = target; return; }
        const float startHz = cur;
        const int chunks = (n + 15) / 16;
        const float wStart = wet, wStep = (wetTarget - wet) / (float) n;
        float* ch[2] = { L, R };
        for (int c = 0; c < chunks; ++c)
        {
            const int a = c * 16, b = std::min (n, a + 16);
            const float t = (float) (c + 1) / (float) chunks;
            const float h = startHz * std::pow (target / startHz, t);
            for (int k = 0; k < 2; ++k) f[(size_t) k].set (h, sr, res, isHp);
            for (int k = 0; k < 2; ++k)
                for (int i = a; i < b; ++i)
                {
                    const float y = f[(size_t) k].tick (ch[k][i]);
                    ch[k][i] += (wStart + wStep * (float) i) * (y - ch[k][i]);
                }
        }
        cur = target;
        wet = wetTarget;
        if (wet <= 0.0f) for (auto& x : f) x.reset();
    }

    float sr = 48000.0f, hpHz_ = 20.0f, lpHz_ = 20000.0f, res = 0.3f;
    float hpCur = 20.0f, lpCur = 20000.0f, hpOn = 0.0f, lpOn = 0.0f;
    std::array<Analog24, 2> hp, lp;
};

// ---------------------------------------------------------------------------
// Reverb de resortes física (modelo paramétrico tipo Välimäki / Parker / Abel).
// Cada resorte es un loop: entrada + realimentación -> 60 allpass "estirados" (a + z^-K)/(1 + a·z^-K)
// -> pasa-bajos -> retardo (ida y vuelta por el resorte, con micro-modulación) -> realimentación.
// Por debajo de la frecuencia de transición (~4 kHz) los allpass estirados retrasan más los agudos
// que los graves (así viajan las ondas en un resorte helicoidal): cada vuelta llega como un "chirp", y como la dispersión está dentro del loop cada repetición es más "boing" que la anterior.
// Tres resortes de distinto largo dan el estéreo (como los tanques de 3 resortes).
class SpringReverb
{
public:
    static constexpr int kSprings = 3;
    static constexpr int kStages  = 60;
    static constexpr int kMaxK    = 16;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        // K: la zona dispersiva queda por debajo de ~4 kHz (fs / 2K), como en un resorte real
        K = std::clamp ((int) std::lround (sr / (2.0f * 4000.0f)), 1, kMaxK);
        for (auto& s : springs)
        {
            s.buf.assign ((size_t) (0.16f * sr) + 8, 0.0f);
            s.w = 0;
            for (auto& st : s.xs) st.fill (0.0f);
            for (auto& st : s.ys) st.fill (0.0f);
            s.idx = 0;
            s.lp.reset(); s.fbHp.reset();
            s.fbHp.set (60.0f, sr, 0.6f);
            s.modState = s.modTarget = 0.0f; s.modCount = 0;
        }
        inHp.reset(); inHp2.reset(); inLp.reset();
        for (auto* a : { &outHp, &outHp2, &outLp, &outLp2, &body }) for (auto& f : *a) f.reset();
        // medido en la referencia: casi nada debajo de 300 Hz ni arriba de 2,5 kHz (transductor + resortes)
        inHp.set (400.0f, sr, 0.54f); inHp2.set (400.0f, sr, 1.31f);    // Butterworth de 4º orden
        inLp.set (4500.0f, sr, 0.6f);
        for (int c = 0; c < 2; ++c) { outHp[(size_t) c].set (300.0f, sr, 0.54f); outHp2[(size_t) c].set (300.0f, sr, 1.31f); }
        setParams (0.5f, 0.5f, 0.5f);
    }

    // size: largo / tensión de los resortes; decay: cola; tone: brillo
    void setParams (float size01, float decay01, float tone01)
    {
        size01 = std::clamp (size01, 0.0f, 1.0f);
        decay01 = std::clamp (decay01, 0.0f, 1.0f);
        tone01 = std::clamp (tone01, 0.0f, 1.0f);
        static const float ratio[kSprings] = { 1.0f, 1.173f, 1.341f };
        const float base = (0.026f + 0.030f * size01) * sr;
        const float rt60 = 1.0f + 5.0f * decay01;   // 1 .. 6 s (con 60 %: ~3,5 s medidos, como la referencia)
        for (int i = 0; i < kSprings; ++i)
        {
            auto& s = springs[(size_t) i];
            s.len = base * ratio[i];
            s.g = std::pow (10.0f, -3.0f * (s.len / sr) / rt60);
            s.lp.set (2200.0f + 2600.0f * tone01, sr, 0.6f);
        }
        // pasa-bajos de salida de 4º orden: con TONO al 50 % corta en ~2,6 kHz
        const float outLpHz = 2200.0f + 1600.0f * tone01;
        for (int c = 0; c < 2; ++c) { outLp[(size_t) c].set (outLpHz, sr, 0.54f); outLp2[(size_t) c].set (outLpHz, sr, 1.31f); }
        a = 0.62f + 0.16f * size01;          // más tensión = chirp más marcado
        for (auto& b : body) b.set (900.0f + 800.0f * tone01, sr, 0.8f);
        bodyGain = 0.6f;
    }

    // in: mono. Suma la reverb (estéreo) en outL/outR con ganancia gain.
    void process (const float* in, float* outL, float* outR, int n, float gain)
    {
        for (int i = 0; i < n; ++i)
        {
            // el transductor de entrada: sin graves, satura un poco con golpes fuertes (el "crash" del tanque)
            float x = inLp.lowpass (inHp2.highpass (inHp.highpass (in[i])));
            x = fastTanh (1.4f * x) * 0.7f;

            float o[kSprings];
            for (int si = 0; si < kSprings; ++si)
            {
                auto& s = springs[(size_t) si];
                // micro-modulación aleatoria del largo (el resorte nunca está quieto)
                if (--s.modCount <= 0) { s.modCount = (int) (sr * 0.15f); s.modTarget = rnd(); }
                s.modState += (s.modTarget - s.modState) * (3.0f / sr);
                const float d = s.len * (1.0f + 0.0016f * s.modState);
                const size_t size = s.buf.size();
                float rp = (float) s.w - d;
                if (rp < 0.0f) rp += (float) size;
                const size_t i0 = (size_t) rp % size, i1 = (i0 + 1) % size;
                const float fr = rp - std::floor (rp);
                const float back = s.buf[i0] + fr * (s.buf[i1] - s.buf[i0]);

                float u = x + s.g * s.fbHp.highpass (back);
                // dispersión: cascada de allpass estirados, un índice circular común
                const int r = s.idx;
                for (int k = 0; k < kStages; ++k)
                {
                    const float xd = s.xs[(size_t) k][(size_t) r], yd = s.ys[(size_t) k][(size_t) r];
                    const float y = a * u + xd - a * yd;
                    s.xs[(size_t) k][(size_t) r] = u;
                    s.ys[(size_t) k][(size_t) r] = y;
                    u = y;
                }
                s.idx = (r + 1) % K;
                u = s.lp.lowpass (u);
                s.buf[s.w] = fastTanh (u);
                s.w = (s.w + 1) % size;
                o[si] = u;
            }

            // estéreo bien abierto (la referencia tiene L/R casi sin correlación)
            float l = 1.1f * (o[0] - 0.35f * o[1] + 0.6f * o[2]);
            float rr = 1.1f * (o[1] - 0.35f * o[0] - 0.6f * o[2]);
            // el "cuerpo" metálico del tanque
            float bl, bb, bh;
            body[0].tick (l, bl, bb, bh);  l += bodyGain * bb;
            body[1].tick (rr, bl, bb, bh); rr += bodyGain * bb;
            l  = outLp2[0].lowpass (outLp[0].lowpass (outHp2[0].highpass (outHp[0].highpass (l))));
            rr = outLp2[1].lowpass (outLp[1].lowpass (outHp2[1].highpass (outHp[1].highpass (rr))));
            outL[i] += gain * 1.6f * l;
            outR[i] += gain * 1.6f * rr;
        }
    }

private:
    struct Spring
    {
        std::vector<float> buf;
        size_t w = 0;
        float len = 1000.0f, g = 0.7f;
        std::array<std::array<float, kMaxK>, kStages> xs {}, ys {};
        int idx = 0;
        SVF2 lp, fbHp;
        float modState = 0.0f, modTarget = 0.0f;
        int modCount = 0;
    };
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 8388608.0f - 1.0f; }

    std::array<Spring, kSprings> springs;
    SVF2 inHp, inHp2, inLp;
    std::array<SVF2, 2> outHp, outHp2, outLp, outLp2, body;
    float sr = 48000.0f, a = 0.65f, bodyGain = 0.6f;
    int K = 6;
    unsigned rng = 12345u;
};

// ---------------------------------------------------------------------------
// Drive (saturación suave) + limitador de pico. Estéreo, en el lugar.
class MasterDynamics
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
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
                env = pk > env ? pk : pk + release * (env - pk);   // ataque instantáneo: nunca recorta
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
    float sr = 48000.0f, drive = 0.0f, ceiling = 0.97f, release = 0.0f, env = 0.0f, grDb = 0.0f;
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
