// Controller.h — todo lo que mueve el preamp desde afuera: acciones (botones), MIDI de la
// DDJ-FLX4 con mapeo de fábrica, MIDI learn para cualquier otro controlador, y luces de la FLX4.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
#include <map>
#include "Engine.h"
#include "Library.h"

namespace ct
{

enum class Kind { Hold, Trigger };

#define CT_ACTIONS(X) \
    X (Play1,      "play1",      "Deck 1: play / pausa",            Trigger) \
    X (Cue1,       "cue1",       "Deck 1: cue",                     Hold)    \
    X (Load1,      "load1",      "Deck 1: cargar tema",             Trigger) \
    X (LoopIn1,    "loopIn1",    "Deck 1: loop in",                 Trigger) \
    X (LoopOut1,   "loopOut1",   "Deck 1: loop out",                Trigger) \
    X (Reloop1,    "reloop1",    "Deck 1: reloop / salir",          Trigger) \
    X (Hot1A,      "hot1a",      "Deck 1: hot cue 1",               Hold)    \
    X (Hot1B,      "hot1b",      "Deck 1: hot cue 2",               Hold)    \
    X (Hot1C,      "hot1c",      "Deck 1: hot cue 3",               Hold)    \
    X (Hot1D,      "hot1d",      "Deck 1: hot cue 4",               Hold)    \
    X (Play2,      "play2",      "Deck 2: play / pausa",            Trigger) \
    X (Cue2,       "cue2",       "Deck 2: cue",                     Hold)    \
    X (Load2,      "load2",      "Deck 2: cargar tema",             Trigger) \
    X (LoopIn2,    "loopIn2",    "Deck 2: loop in",                 Trigger) \
    X (LoopOut2,   "loopOut2",   "Deck 2: loop out",                Trigger) \
    X (Reloop2,    "reloop2",    "Deck 2: reloop / salir",          Trigger) \
    X (Hot2A,      "hot2a",      "Deck 2: hot cue 1",               Hold)    \
    X (Hot2B,      "hot2b",      "Deck 2: hot cue 2",               Hold)    \
    X (Hot2C,      "hot2c",      "Deck 2: hot cue 3",               Hold)    \
    X (Hot2D,      "hot2d",      "Deck 2: hot cue 4",               Hold)    \
    X (BrowseUp,   "browseUp",   "Biblioteca: subir",               Trigger) \
    X (BrowseDown, "browseDown", "Biblioteca: bajar",               Trigger) \
    X (BrowseFocus,"browseFocus","Biblioteca: carpetas / temas",    Trigger) \
    X (EchoThrow,  "echoThrow",  "Echo: throw (mantener)",          Hold)    \
    X (EchoFreeze, "echoFreeze", "Echo: freeze (mantener)",         Hold)    \
    X (EchoKill,   "echoKill",   "Echo: kill (cortar repeticiones)",Trigger) \
    X (EchoLatchT, "echoLatchT", "Echo: envío fijo on/off",         Trigger) \
    X (Tap,        "tap",        "Echo: tap tempo",                 Trigger) \
    X (Div1,       "div1",       "Echo: 1/16",                      Trigger) \
    X (Div2,       "div2",       "Echo: 1/8",                       Trigger) \
    X (Div3,       "div3",       "Echo: 1/8 con puntillo",          Trigger) \
    X (Div4,       "div4",       "Echo: 1/4",                       Trigger) \
    X (Div5,       "div5",       "Echo: 1/4 con puntillo",          Trigger) \
    X (Div6,       "div6",       "Echo: 1/2",                       Trigger) \
    X (Div7,       "div7",       "Echo: 1/4 tresillo",              Trigger) \
    X (EchoShorter,"echoShorter","Echo: tiempo más corto (mantener)", Hold)  \
    X (EchoLonger, "echoLonger", "Echo: tiempo más largo (mantener)", Hold)  \
    X (BothThrow,  "bothThrow",  "Echo + spring: throw (mantener)", Hold)    \
    X (SrcCh1,     "srcCh1",     "Envíos: desde canal 1",           Trigger) \
    X (SrcCh2,     "srcCh2",     "Envíos: desde canal 2",           Trigger) \
    X (SrcBoth,    "srcBoth",    "Envíos: desde los dos canales",   Trigger) \
    X (IsoModeNext,"isoModeNext","Aislador: cambiar vías (3/4/5)",  Trigger) \
    X (RevThrow,   "revThrow",   "Spring: throw (mantener)",        Hold)    \
    X (RevLatchT,  "revLatchT",  "Spring: envío fijo on/off",       Trigger) \
    X (Siren1,     "siren1",     "Sirena: dos tonos",               Hold)    \
    X (Siren2,     "siren2",     "Sirena: láser",                   Hold)    \
    X (Siren3,     "siren3",     "Sirena: ondulante",               Hold)    \
    X (Siren4,     "siren4",     "Sirena: caída",                   Hold)    \
    X (Siren5,     "siren5",     "Sirena: sub drop",                Hold)    \
    X (KillT1,     "killT1",     "Aislador: kill banda 1",          Trigger) \
    X (KillT2,     "killT2",     "Aislador: kill banda 2",          Trigger) \
    X (KillT3,     "killT3",     "Aislador: kill banda 3",          Trigger) \
    X (KillT4,     "killT4",     "Aislador: kill banda 4",          Trigger) \
    X (KillT5,     "killT5",     "Aislador: kill banda 5",          Trigger) \
    X (Flat,       "flat",       "Aislador: FLAT",                  Trigger) \
    X (SubBoostT,  "subBoostT",  "Aislador: sub boost on/off",      Trigger) \
    X (Smp1,  "smp1",  "Sample 1",  Trigger) X (Smp2,  "smp2",  "Sample 2",  Trigger) \
    X (Smp3,  "smp3",  "Sample 3",  Trigger) X (Smp4,  "smp4",  "Sample 4",  Trigger) \
    X (Smp5,  "smp5",  "Sample 5",  Trigger) X (Smp6,  "smp6",  "Sample 6",  Trigger) \
    X (Smp7,  "smp7",  "Sample 7",  Trigger) X (Smp8,  "smp8",  "Sample 8",  Trigger) \
    X (Smp9,  "smp9",  "Sample 9",  Trigger) X (Smp10, "smp10", "Sample 10", Trigger) \
    X (Smp11, "smp11", "Sample 11", Trigger) X (Smp12, "smp12", "Sample 12", Trigger) \
    X (Smp13, "smp13", "Sample 13", Trigger) X (Smp14, "smp14", "Sample 14", Trigger) \
    X (Smp15, "smp15", "Sample 15", Trigger) X (Smp16, "smp16", "Sample 16", Trigger) \
    X (SmpStop,    "smpStop",    "Samples: parar todo",             Trigger)

enum class A : int
{
   #define CT_AENUM(name, key, label, kind) name,
    CT_ACTIONS (CT_AENUM)
   #undef CT_AENUM
    Count
};
constexpr int kNumActions = (int) A::Count;

struct ActionInfo { const char* key; const char* label; Kind kind; };
inline const ActionInfo& actionInfo (A a)
{
    static const ActionInfo table[] = {
       #define CT_AINFO(name, key, label, kind) { key, label, Kind::kind },
        CT_ACTIONS (CT_AINFO)
       #undef CT_AINFO
    };
    return table[(int) a];
}

// Destino de un mapeo MIDI aprendido: parámetro (0..kNumParams-1) o acción (1000 + índice).
constexpr int kActionBase = 1000;

class Controller : public juce::MidiInputCallback, private juce::Timer
{
public:
    Controller (Engine&, Library&, juce::AudioDeviceManager&);
    ~Controller() override;

    // se llaman en el hilo de mensajes
    std::function<void (int deck)> onLoadRequest;
    std::function<void (const juce::String&)> onStatus;

    // ejecutar una acción (pantalla, teclado o MIDI). src = Hold::Src
    void perform (A a, int src, bool down);

    // MIDI learn
    void startLearn (int target)          { learnTarget.store (target); learnCands.clear(); }
    void cancelLearn()                    { learnTarget.store (-1); }
    int  getLearnTarget() const           { return learnTarget.load(); }
    juce::String describeMappingFor (int target) const;
    void clearMappingFor (int target);
    juce::String getLastMidi() const      { const juce::SpinLock::ScopedLockType sl (lastLock); return lastMidi; }

    bool isFlx4Connected() const          { return flx4In.load(); }
    bool isFlx4MappingOn() const          { return flx4Enabled.load(); }
    void setFlx4MappingOn (bool on)       { flx4Enabled.store (on); }
    bool isTempoInverted() const          { return tempoInvert.load(); }
    bool isNanoMappingOn() const          { return nanoEnabled.load(); }
    void setNanoMappingOn (bool on)       { nanoEnabled.store (on); }
    bool isNanoConnected() const          { return nanoIn.load(); }
    int  getFlx4PadMode (int deck) const  { return flx4PadMode[deck].load(); }
    void setTempoInverted (bool inv)      { tempoInvert.store (inv); }

    juce::var mappingsToVar() const;
    void mappingsFromVar (const juce::var&);

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    // entrada común (también la usan las pruebas, con el nombre del aparato)
    void dispatch (const juce::String& deviceName, const juce::MidiMessage&);

private:
    void timerCallback() override;
    void handleFlx4 (const juce::MidiMessage&);
    void handleNano (const juce::MidiMessage&);
    void confirmFlx4PadMode (int deck, int modeNote);
    bool handleLearned (const juce::MidiMessage&, int key, float value01, bool press, bool isButton);
    void applyTarget (int target, float value01, bool press, bool isButton, int src);
    void updateLeds();
    void sendLed (int status, int note, int value);
    void runOnMessageThread (std::function<void()>);

    static int packKey (int type, int channel, int number) { return (type << 16) | (channel << 8) | number; }

    Engine& engine;
    Library& library;
    juce::AudioDeviceManager& devices;

    // mapeos aprendidos: clave MIDI -> destino
    mutable juce::SpinLock mapLock;
    std::map<int, int> learned;
    std::atomic<int> learnTarget { -1 };
    struct Cand { int key; float minV, maxV; bool button; };
    std::vector<Cand> learnCands;
    double learnStart = 0.0;
    std::map<int, bool> toggleLast;   // para CC usados como botón

    // estado FLX4
    std::atomic<bool> flx4Enabled { true }, flx4In { false }, tempoInvert { false };
    std::atomic<bool> nanoEnabled { true }, nanoIn { false };
    std::atomic<int> flx4PadMode[2] { { 0x1B }, { 0x1B } };   // botón de modo activo (0x1B = HOT CUE)
    std::atomic<bool> flx4ModeDirty { true };
    double fxOnOffPressMs[2] { 0.0, 0.0 };
    float nanoLast[2] { -1.0f, -1.0f };   // último valor de los ejes X / Y del nanoPAD2
    bool shift[2] { false, false };
    int msb[16][32] {};
    std::unique_ptr<juce::MidiOutput> flx4Out;
    std::map<int, int> ledCache;
    int ticks = 0;

    mutable juce::SpinLock lastLock;
    juce::String lastMidi;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Controller)
};

} // namespace ct
