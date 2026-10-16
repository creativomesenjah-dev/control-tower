// Params.h — todos los controles continuos y de estado del preamp.
// Cada parámetro es un atomic (valor real, en sus unidades): lo leen el audio, la interfaz,
// el MIDI y el guardado sin locks.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace ct
{

inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

enum class Unit { Db, Pct, Hz, Ms, Sec, Eq, Bi, Choice, Toggle, Raw };

//   id interno,  clave,        nombre,                     min,   max,   defecto, centro(skew), unidad
#define CT_PARAMS(X) \
    X (TrimA,       "trimA",      "Canal 1: trim",              -12.f,  12.f,   0.f,    0.f,   Db)     \
    X (HiA,         "hiA",        "Canal 1: agudos",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (MidA,        "midA",       "Canal 1: medios",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (LowA,        "lowA",       "Canal 1: graves",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (FilterA,     "filterA",    "Canal 1: filtro",              0.f,   1.f,   .5f,    0.f,   Bi)     \
    X (FaderA,      "faderA",     "Canal 1: fader",               0.f,   1.f,   1.f,    0.f,   Raw)    \
    X (PitchA,      "pitchA",     "Deck 1: pitch",               -1.f,   1.f,   0.f,    0.f,   Raw)    \
    X (RangeA,      "rangeA",     "Deck 1: rango de pitch",       0.f,   2.f,   0.f,    0.f,   Choice) \
    X (CueA,        "cueA",       "Canal 1: cue auriculares",     0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (TrimB,       "trimB",      "Canal 2: trim",              -12.f,  12.f,   0.f,    0.f,   Db)     \
    X (HiB,         "hiB",        "Canal 2: agudos",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (MidB,        "midB",       "Canal 2: medios",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (LowB,        "lowB",       "Canal 2: graves",              0.f,   1.f,   .5f,    0.f,   Eq)     \
    X (FilterB,     "filterB",    "Canal 2: filtro",              0.f,   1.f,   .5f,    0.f,   Bi)     \
    X (FaderB,      "faderB",     "Canal 2: fader",               0.f,   1.f,   1.f,    0.f,   Raw)    \
    X (PitchB,      "pitchB",     "Deck 2: pitch",               -1.f,   1.f,   0.f,    0.f,   Raw)    \
    X (RangeB,      "rangeB",     "Deck 2: rango de pitch",       0.f,   2.f,   0.f,    0.f,   Choice) \
    X (CueB,        "cueB",       "Canal 2: cue auriculares",     0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (Crossfader,  "xfader",     "Crossfader",                   0.f,   1.f,   .5f,    0.f,   Raw)    \
    X (HeadMix,     "headMix",    "Auriculares: cue / master",    0.f,   1.f,   0.f,    0.f,   Pct)    \
    X (HeadLevel,   "headLevel",  "Auriculares: nivel",         -40.f,   6.f,   0.f,  -10.f,   Db)     \
    X (IsoMode,     "isoMode",    "Aislador: vías",               0.f,   2.f,   1.f,    0.f,   Choice) \
    X (Iso1,        "iso1",       "Aislador: banda 1",          -40.f,  12.f,   0.f,   -6.f,   Db)     \
    X (Iso2,        "iso2",       "Aislador: banda 2",          -40.f,  12.f,   0.f,   -6.f,   Db)     \
    X (Iso3,        "iso3",       "Aislador: banda 3",          -40.f,  12.f,   0.f,   -6.f,   Db)     \
    X (Iso4,        "iso4",       "Aislador: banda 4",          -40.f,  12.f,   0.f,   -6.f,   Db)     \
    X (Iso5,        "iso5",       "Aislador: banda 5",          -40.f,  12.f,   0.f,   -6.f,   Db)     \
    X (Kill1,       "kill1",      "Aislador: kill banda 1",       0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (Kill2,       "kill2",      "Aislador: kill banda 2",       0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (Kill3,       "kill3",      "Aislador: kill banda 3",       0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (Kill4,       "kill4",      "Aislador: kill banda 4",       0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (Kill5,       "kill5",      "Aislador: kill banda 5",       0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (SubBoost,    "subBoost",   "Aislador: sub boost",          0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (MFilter,     "mFilter",    "Master: filtro",               0.f,   1.f,   .5f,    0.f,   Bi)     \
    X (MRes,        "mRes",       "Master: resonancia",           0.f,   1.f,   .3f,    0.f,   Pct)    \
    X (Drive,       "drive",      "Master: drive",                0.f,   1.f,   0.f,    0.f,   Pct)    \
    X (MLevel,      "mLevel",     "Master: nivel",              -40.f,   6.f,   0.f,  -10.f,   Db)     \
    X (Limiter,     "limiter",    "Master: limitador",            0.f,   1.f,   1.f,    0.f,   Toggle) \
    X (EchoTime,    "echoTime",   "Echo: tiempo",                20.f, 2000.f, 450.f,  450.f,   Ms)     \
    X (EchoFb,      "echoFb",     "Echo: repeticiones",           0.f, 110.f,  55.f,    0.f,   Pct)    \
    X (EchoSend,    "echoSendLvl","Echo: nivel del envío",        0.f,   1.f,   1.f,    0.f,   Pct)    \
    X (EchoLatch,   "echoLatch",  "Echo: envío fijo",             0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (EchoRet,     "echoRet",    "Echo: retorno",              -40.f,   6.f,   0.f,  -10.f,   Db)     \
    X (EchoSrc,     "echoSrc",    "Echo / spring: fuente",        0.f,   2.f,   2.f,    0.f,   Choice) \
    X (EchoLow,     "echoLow",    "Echo: corte de graves",       20.f, 1000.f, 180.f,  150.f,   Hz)     \
    X (EchoHigh,    "echoHigh",   "Echo: corte de agudos",      800.f, 12000.f, 3200.f, 3000.f, Hz)     \
    X (EchoWow,     "echoWow",    "Echo: wow",                    0.f, 100.f,  25.f,    0.f,   Pct)    \
    X (EchoSat,     "echoSat",    "Echo: saturación",             0.f, 100.f,  35.f,    0.f,   Pct)    \
    X (EchoIner,    "echoIner",   "Echo: inercia",              0.02f,   2.f, .35f,   .35f,   Sec)    \
    X (RevSend,     "revSendLvl", "Spring: nivel del envío",      0.f,   1.f,   1.f,    0.f,   Pct)    \
    X (RevLatch,    "revLatch",   "Spring: envío fijo",           0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (RevRet,      "revRet",     "Spring: retorno",            -40.f,   6.f,  -3.f,  -10.f,   Db)     \
    X (RevSize,     "revSize",    "Spring: cuerpo",               0.f,   1.f,   .5f,    0.f,   Pct)    \
    X (RevDecay,    "revDecay",   "Spring: cola",                 0.f,   1.f,   .6f,    0.f,   Pct)    \
    X (RevTone,     "revTone",    "Spring: tono",                 0.f,   1.f,   .5f,    0.f,   Pct)    \
    X (EchoToRev,   "echoToRev",  "Echo al spring",               0.f,   1.f,  .25f,    0.f,   Pct)    \
    X (SirenPitch,  "sirenPitch", "Sirena: tono",                 0.f,   1.f,   .5f,    0.f,   Pct)    \
    X (SirenRate,   "sirenRate",  "Sirena: velocidad",            0.f,   1.f,   .5f,    0.f,   Pct)    \
    X (SirenLevel,  "sirenLevel", "Sirena: volumen",            -40.f,   6.f,  -6.f,  -10.f,   Db)     \
    X (SirenEcho,   "sirenEcho",  "Sirena: al echo",              0.f,   1.f,   .5f,    0.f,   Pct)    \
    X (SampLevel,   "sampLevel",  "Samples: volumen",           -40.f,   6.f,  -3.f,  -10.f,   Db)     \
    X (SampEcho,    "sampEcho",   "Samples: al echo",             0.f,   1.f,   .3f,    0.f,   Pct)    \
    X (InOn,        "inOn",       "Entrada en vivo: on",          0.f,   1.f,   0.f,    0.f,   Toggle) \
    X (InGain,      "inGain",     "Entrada en vivo: ganancia",  -40.f,  12.f,   0.f,  -10.f,   Db)     \
    X (InEcho,      "inEcho",     "Entrada en vivo: al echo",     0.f,   1.f,   0.f,    0.f,   Pct)

enum class P : int
{
   #define CT_ENUM(name, key, label, mn, mx, def, centre, unit) name,
    CT_PARAMS (CT_ENUM)
   #undef CT_ENUM
    Count
};

constexpr int kNumParams = (int) P::Count;

struct ParamInfo
{
    const char* key;
    const char* label;
    float min, max, def, centre;
    Unit unit;
};

inline const ParamInfo& paramInfo (P p)
{
    static const ParamInfo table[] = {
       #define CT_INFO(name, key, label, mn, mx, def, centre, unit) { key, label, mn, mx, def, centre, Unit::unit },
        CT_PARAMS (CT_INFO)
       #undef CT_INFO
    };
    return table[(int) p];
}

class Params
{
public:
    Params()
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& inf = paramInfo ((P) i);
            values[(size_t) i].store (inf.def);
            ranges[(size_t) i] = juce::NormalisableRange<float> (inf.min, inf.max);
            if (inf.centre > inf.min && inf.centre < inf.max)
                ranges[(size_t) i].setSkewForCentre (inf.centre);
            if (inf.unit == Unit::Choice || inf.unit == Unit::Toggle)
                ranges[(size_t) i].interval = 1.0f;
        }
    }

    float get (P p) const            { return values[(size_t) p].load (std::memory_order_relaxed); }
    bool  on (P p) const             { return get (p) > 0.5f; }
    int   choice (P p) const         { return juce::roundToInt (get (p)); }
    void  set (P p, float v)         { values[(size_t) p].store (ranges[(size_t) p].snapToLegalValue (v)); }
    void  toggle (P p)               { set (p, on (p) ? 0.0f : 1.0f); }
    float getNorm (P p) const        { return ranges[(size_t) p].convertTo0to1 (get (p)); }
    void  setNorm (P p, float n)     { set (p, ranges[(size_t) p].convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }
    const juce::NormalisableRange<float>& range (P p) const { return ranges[(size_t) p]; }

    juce::String format (P p) const { return formatValue (p, get (p)); }

    static juce::String formatValue (P p, float v)
    {
        const auto& inf = paramInfo (p);
        switch (inf.unit)
        {
            case Unit::Db:
                if (v <= inf.min + 0.01f && inf.min <= -40.0f) return "OFF";
                return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
            case Unit::Pct:   return juce::String (juce::roundToInt (v * (inf.max > 1.5f ? 1.0f : 100.0f))) + "%";
            case Unit::Hz:    return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            case Unit::Ms:    return juce::String (juce::roundToInt (v)) + " ms";
            case Unit::Sec:   return juce::String (v, 2) + " s";
            case Unit::Eq:
                if (v < 0.02f) return "KILL";
                if (std::abs (v - 0.5f) < 0.02f) return "0 dB";
                return juce::String (juce::Decibels::gainToDecibels (eqGain (v), -60.0f), 1) + " dB";
            case Unit::Bi:
                if (std::abs (v - 0.5f) < 0.02f) return "OFF";
                return v < 0.5f ? "LPF " + juce::String (juce::roundToInt ((0.5f - v) * 200.0f)) + "%"
                                : "HPF " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + "%";
            case Unit::Choice:
            case Unit::Toggle:
            case Unit::Raw:
            default:          return juce::String (v, 2);
        }
    }

    // EQ de canal estilo aislador: 0 = kill, 0.5 = 0 dB, 1 = +6 dB
    static float eqGain (float v)
    {
        if (v < 0.02f) return 0.0f;
        if (std::abs (v - 0.5f) < 0.02f) return 1.0f;
        if (v < 0.5f) { const float t = v / 0.5f; return t * t; }
        return 1.0f + (v - 0.5f) * 2.0f;
    }

    // --- guardado
    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        for (int i = 0; i < kNumParams; ++i)
            obj->setProperty (paramInfo ((P) i).key, get ((P) i));
        return juce::var (obj);
    }

    void fromVar (const juce::var& v)
    {
        if (auto* obj = v.getDynamicObject())
            for (int i = 0; i < kNumParams; ++i)
                if (obj->hasProperty (paramInfo ((P) i).key))
                    set ((P) i, (float) obj->getProperty (paramInfo ((P) i).key));
    }

    static int findByKey (const juce::String& key)
    {
        for (int i = 0; i < kNumParams; ++i)
            if (key == paramInfo ((P) i).key) return i;
        return -1;
    }

private:
    std::array<std::atomic<float>, kNumParams> values;
    std::array<juce::NormalisableRange<float>, kNumParams> ranges;
};

// Estado "mantener" que pueden pedir varias fuentes a la vez (teclado, mouse, MIDI).
struct Hold
{
    enum Src { Keyboard = 1, Mouse = 2, Midi = 4, Midi2 = 8 };
    std::atomic<int> mask { 0 };
    void set (int src, bool down) { if (down) mask.fetch_or (src); else mask.fetch_and (~src); }
    bool held() const { return mask.load() != 0; }
    void clear (int src) { mask.fetch_and (~src); }
};

} // namespace ct
