#include "Controller.h"

namespace ct
{

namespace
{
    bool isFlx4Name (const juce::String& n) { return n.containsIgnoreCase ("DDJ-FLX4") || n.containsIgnoreCase ("DDJ FLX4"); }
    bool isNanoName (const juce::String& n) { return n.containsIgnoreCase ("nanoPAD"); }
}

Controller::Controller (Engine& e, Library& l, juce::AudioDeviceManager& dm)
    : engine (e), library (l), devices (dm)
{
    startTimerHz (20);
}

Controller::~Controller()
{
    *alive = false;
    stopTimer();
    // apagar las luces al salir
    if (flx4Out != nullptr)
        for (auto& [k, v] : ledCache)
            if (v != 0) flx4Out->sendMessageNow (juce::MidiMessage ((k >> 8) & 0xff, k & 0xff, 0));
}

void Controller::runOnMessageThread (std::function<void()> fn)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) { fn(); return; }
    auto flag = alive;
    juce::MessageManager::callAsync ([flag, fn] { if (*flag) fn(); });
}

// ---------------------------------------------------------------------------
void Controller::perform (A a, int src, bool down)
{
    auto& p = engine.params;
    const int ai = (int) a;

    auto deckCmd = [this] (int d, DeckCmd c, int idx = 0, double v = 1.0) { engine.deck (d).pushCommand ({ c, idx, v }); };

    if (a >= A::Smp1 && a <= A::Smp16)
    {
        if (down) engine.getSampler().trigger (ai - (int) A::Smp1);
        return;
    }

    switch (a)
    {
        case A::Play1: case A::Play2:   if (down) deckCmd (a == A::Play1 ? 0 : 1, DeckCmd::PlayToggle); return;
        case A::Cue1:  case A::Cue2:    deckCmd (a == A::Cue1 ? 0 : 1, DeckCmd::Cue, 0, down ? 1.0 : 0.0); return;
        case A::Load1: case A::Load2:
            if (down) { const int d = a == A::Load1 ? 0 : 1; runOnMessageThread ([this, d] { if (onLoadRequest) onLoadRequest (d); }); }
            return;
        case A::LoopIn1:  case A::LoopIn2:  if (down) deckCmd (a == A::LoopIn1 ? 0 : 1, DeckCmd::LoopIn); return;
        case A::LoopOut1: case A::LoopOut2: if (down) deckCmd (a == A::LoopOut1 ? 0 : 1, DeckCmd::LoopOut); return;
        case A::Reloop1:  case A::Reloop2:  if (down) deckCmd (a == A::Reloop1 ? 0 : 1, DeckCmd::Reloop); return;
        case A::Hot1A: case A::Hot1B: case A::Hot1C: case A::Hot1D:
            deckCmd (0, DeckCmd::Hotcue, ai - (int) A::Hot1A, down ? 1.0 : 0.0);
            return;
        case A::Hot2A: case A::Hot2B: case A::Hot2C: case A::Hot2D:
            deckCmd (1, DeckCmd::Hotcue, ai - (int) A::Hot2A, down ? 1.0 : 0.0);
            return;

        case A::BrowseUp:    if (down) runOnMessageThread ([this] { library.moveSelection (-1); }); return;
        case A::BrowseDown:  if (down) runOnMessageThread ([this] { library.moveSelection (1); }); return;
        case A::BrowseFocus: if (down) runOnMessageThread ([this] { library.toggleFocus(); }); return;

        case A::EchoThrow:  engine.echoThrow.set (src, down); return;
        case A::EchoFreeze: engine.echoFreeze.set (src, down); return;
        case A::RevThrow:   engine.revThrow.set (src, down); return;
        case A::BothThrow:  engine.echoThrow.set (src, down); engine.revThrow.set (src, down); return;
        case A::EchoShorter: engine.echoShorter.set (src, down); return;
        case A::EchoLonger:  engine.echoLonger.set (src, down); return;
        case A::SrcCh1:  if (down) p.set (P::EchoSrc, 0.0f); return;
        case A::SrcCh2:  if (down) p.set (P::EchoSrc, 1.0f); return;
        case A::SrcBoth: if (down) p.set (P::EchoSrc, 2.0f); return;
        case A::IsoModeNext: if (down) p.set (P::IsoMode, (float) ((p.choice (P::IsoMode) + 1) % 3)); return;
        case A::EchoKill:   if (down) engine.killEcho(); return;
        case A::EchoLatchT: if (down) p.toggle (P::EchoLatch); return;
        case A::RevLatchT:  if (down) p.toggle (P::RevLatch); return;
        case A::Tap:        if (down) engine.tap(); return;
        case A::Div1: case A::Div2: case A::Div3: case A::Div4: case A::Div5: case A::Div6: case A::Div7:
            if (down) engine.selectEchoDivision (ai - (int) A::Div1);
            return;

        case A::Siren1: case A::Siren2: case A::Siren3: case A::Siren4: case A::Siren5:
            engine.setSiren (ai - (int) A::Siren1, src, down);
            return;

        case A::KillT1: case A::KillT2: case A::KillT3: case A::KillT4: case A::KillT5:
            if (down) p.toggle ((P) ((int) P::Kill1 + ai - (int) A::KillT1));
            return;
        case A::Flat:
            if (down)
                for (int b = 0; b < 5; ++b)
                {
                    p.set ((P) ((int) P::Iso1 + b), 0.0f);
                    p.set ((P) ((int) P::Kill1 + b), 0.0f);
                }
            return;
        case A::SubBoostT: if (down) p.toggle (P::SubBoost); return;

        case A::SmpStop: if (down) engine.getSampler().stopAll(); return;
        case A::Smp1: case A::Smp2: case A::Smp3: case A::Smp4: case A::Smp5: case A::Smp6: case A::Smp7: case A::Smp8:
        case A::Smp9: case A::Smp10: case A::Smp11: case A::Smp12: case A::Smp13: case A::Smp14: case A::Smp15: case A::Smp16:
        case A::Count: return;
    }
}

// ---------------------------------------------------------------------------
void Controller::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    dispatch (source != nullptr ? source->getName() : juce::String(), m);
}

void Controller::dispatch (const juce::String& deviceName, const juce::MidiMessage& m)
{
    if (m.isMidiClock() || m.isActiveSense() || m.isSysEx()) return;

    int type = 0, number = 0;
    float value = 0.0f;
    bool press = false, isButton = false;
    if (m.isNoteOn())          { type = 1; number = m.getNoteNumber(); value = m.getVelocity() / 127.0f; press = true; isButton = true; }
    else if (m.isNoteOff())    { type = 1; number = m.getNoteNumber(); value = 0.0f; press = false; isButton = true; }
    else if (m.isController()) { type = 2; number = m.getControllerNumber(); value = m.getControllerValue() / 127.0f; press = m.getControllerValue() >= 64; }
    else if (m.isPitchWheel()) { type = 3; value = m.getPitchWheelValue() / 16383.0f; }
    else return;

    const int key = packKey (type, m.getChannel(), number);
    const bool fromFlx4 = isFlx4Name (deviceName);
    const bool fromNano = isNanoName (deviceName);

    {
        const juce::SpinLock::ScopedLockType sl (lastLock);
        lastMidi = (deviceName.isNotEmpty() ? deviceName + ": " : juce::String())
                 + (type == 1 ? "nota " + juce::String (number) : type == 2 ? "CC " + juce::String (number) : juce::String ("pitch bend"))
                 + u8 (" · canal ") + juce::String (m.getChannel()) + " = " + juce::String (juce::roundToInt (value * 127.0f));
    }

    if (handleLearned (m, key, value, press, isButton)) return;

    if (fromFlx4 && flx4Enabled.load())
        handleFlx4 (m);
    else if (fromNano && nanoEnabled.load())
        handleNano (m);
}

bool Controller::handleLearned (const juce::MidiMessage&, int key, float value, bool press, bool isButton)
{
    const int target = learnTarget.load();
    if (target >= 0)
    {
        const bool continuous = target < kActionBase
                             && paramInfo ((P) target).unit != Unit::Toggle;
        const juce::SpinLock::ScopedLockType sl (mapLock);
        if (continuous)
        {
            if (isButton) return true;
            if (learnCands.empty()) learnStart = juce::Time::getMillisecondCounterHiRes();
            for (auto& c : learnCands)
                if (c.key == key) { c.minV = juce::jmin (c.minV, value); c.maxV = juce::jmax (c.maxV, value); return true; }
            learnCands.push_back ({ key, value, value, false });
            return true;
        }
        if (! press) return true;
        for (auto it = learned.begin(); it != learned.end();)
            it = (it->second == target) ? learned.erase (it) : std::next (it);
        learned[key] = target;
        learnTarget.store (-1);
        return true;
    }

    int t = -1;
    {
        const juce::SpinLock::ScopedLockType sl (mapLock);
        auto it = learned.find (key);
        if (it == learned.end()) return false;
        t = it->second;
        // un CC usado como botón: sólo actúa en el flanco
        if (! isButton)
        {
            const bool last = toggleLast[key];
            toggleLast[key] = press;
            const bool needsEdge = t >= kActionBase || paramInfo ((P) t).unit == Unit::Toggle;
            if (needsEdge && last == press) return true;
        }
    }
    applyTarget (t, value, press, isButton, Hold::Midi);
    return true;
}

void Controller::applyTarget (int target, float value01, bool press, bool, int src)
{
    if (target >= kActionBase)
    {
        perform ((A) (target - kActionBase), src, press);
        return;
    }
    auto p = (P) target;
    // mientras suena una sirena, los controles de tiempo y feedback del echo tocan la sirena
    if (engine.isSirenHeld())
    {
        if (p == P::EchoTime) p = P::SirenPitch;
        else if (p == P::EchoFb) p = P::SirenRate;
    }
    if (paramInfo (p).unit == Unit::Toggle) { if (press) engine.params.toggle (p); }
    else engine.params.setNorm (p, value01);
}

// ---------------------------------------------------------------------------
// Mapeo de fábrica de la Pioneer DDJ-FLX4 (tabla de mensajes tomada del mapeo libre de Mixxx).
void Controller::handleFlx4 (const juce::MidiMessage& m)
{
    const auto* raw = m.getRawData();
    if (m.getRawDataSize() < 3) return;
    const int status = raw[0], d1 = raw[1], d2 = raw[2];
    const int kind = status & 0xF0, ch = status & 0x0F;
    auto& p = engine.params;
    const bool on = kind == 0x90 && d2 > 0;

    // ---------------- perillas, faders y jogs (CC)
    if (kind == 0xB0)
    {
        // jogs y browse: relativos, no son 14 bits
        if (ch <= 1)
        {
            const int d = ch;
            if (d1 == 0x22 || d1 == 0x23) { engine.deck (d).pushCommand ({ DeckCmd::JogScratch, 0, (double) (d2 - 64) }); return; }
            if (d1 == 0x21)               { engine.deck (d).pushCommand ({ DeckCmd::JogBend, 0, (double) (d2 - 64) }); return; }
            if (d1 == 0x29)               { engine.deck (d).pushCommand ({ DeckCmd::JogSearch, 0, (double) (d2 - 64) }); return; }
        }
        if (ch == 6 && (d1 == 0x40 || d1 == 0x64))
        {
            const int delta = (d2 < 64 ? d2 : d2 - 128) * (d1 == 0x64 ? 10 : 1);
            runOnMessageThread ([this, delta] { library.moveSelection (delta); });
            return;
        }
        if (ch == 4 && d1 == 0x02) { p.setNorm (P::EchoFb, d2 / 127.0f); return; }   // BEAT FX LEVEL/DEPTH

        // 14 bits: MSB en cc, LSB en cc + 0x20
        if (d1 >= 0x40) return;
        int cc = d1;
        float v;
        if (d1 < 0x20) { msb[ch][d1] = d2; v = (float) (d2 << 7) / 16383.0f; }
        else           { cc = d1 - 0x20; v = (float) ((msb[ch][cc] << 7) | d2) / 16383.0f; }

        if (ch <= 1)
        {
            const bool a = ch == 0;
            switch (cc)
            {
                case 0x13: p.setNorm (a ? P::FaderA : P::FaderB, v); break;
                case 0x04: p.setNorm (a ? P::TrimA : P::TrimB, v); break;
                case 0x07: p.setNorm (a ? P::HiA : P::HiB, v); break;
                case 0x0B: p.setNorm (a ? P::MidA : P::MidB, v); break;
                case 0x0F: p.setNorm (a ? P::LowA : P::LowB, v); break;
                case 0x00:
                {
                    const float pitch = tempoInvert.load() ? 2.0f * v - 1.0f : 1.0f - 2.0f * v;
                    p.set (a ? P::PitchA : P::PitchB, std::abs (pitch) < 0.004f ? 0.0f : pitch);
                    break;
                }
                default: break;
            }
        }
        else if (ch == 6)
        {
            switch (cc)
            {
                case 0x1F: p.setNorm (P::Crossfader, v); break;
                case 0x0C: p.setNorm (P::HeadMix, v); break;
                case 0x17: p.setNorm (P::FilterA, v); break;
                case 0x18: p.setNorm (P::FilterB, v); break;
                default: break;
            }
        }
        return;
    }

    if (kind != 0x90 && kind != 0x80) return;
    const bool press = on;

    // ---------------- botones de deck (canal 1 / 2)
    if (ch <= 1)
    {
        const int d = ch;
        auto cmd = [&] (DeckCmd c, int idx = 0, double v = 1.0) { engine.deck (d).pushCommand ({ c, idx, v }); };
        switch (d1)
        {
            case 0x3F: shift[d] = press; break;
            case 0x0B: if (press) cmd (DeckCmd::PlayToggle); break;
            case 0x0C: cmd (DeckCmd::Cue, 0, press ? 1.0 : 0.0); break;
            case 0x48: if (press) cmd (DeckCmd::JumpStart); break;
            case 0x10: if (press) cmd (DeckCmd::LoopIn); break;
            case 0x11: if (press) cmd (DeckCmd::LoopOut); break;
            case 0x4D: if (press) cmd (DeckCmd::Reloop); break;
            case 0x51: if (press) cmd (DeckCmd::LoopHalve); break;
            case 0x53: if (press) cmd (DeckCmd::LoopDouble); break;
            case 0x58: if (press) engine.tap(); break;
            case 0x60:
                if (press)
                {
                    const P r = d == 0 ? P::RangeA : P::RangeB;
                    p.set (r, (float) ((p.choice (r) + 1) % 3));
                    static const char* names[] = { "±8%", "±16%", "±50%" };
                    const auto msg = "Deck " + juce::String (d + 1) + ": rango de pitch " + u8 (names[p.choice (r)]);
                    runOnMessageThread ([this, msg] { if (onStatus) onStatus (msg); });
                }
                break;
            case 0x54: if (press) p.toggle (d == 0 ? P::CueA : P::CueB); break;
            case 0x36: case 0x67: cmd (DeckCmd::JogTouch, 0, press ? 1.0 : 0.0); break;
            // botones de modo de pad: la FLX4 sólo cambia de modo cuando el programa le enciende la luz
            case 0x1B: case 0x69: case 0x1E: case 0x6B: case 0x20: case 0x6D: case 0x22: case 0x6F:
                if (press) confirmFlx4PadMode (d, d1);
                break;
            default: break;
        }
        return;
    }

    // ---------------- browse y load
    if (ch == 6)
    {
        if (! press) return;
        if (d1 == 0x46) perform (A::Load1, Hold::Midi, true);
        else if (d1 == 0x47) perform (A::Load2, Hold::Midi, true);
        else if (d1 == 0x41) runOnMessageThread ([this] { library.toggleFocus(); });
        else if (d1 == 0x42) runOnMessageThread ([this] { library.setTrackFocus (false); });
        return;
    }

    // ---------------- sección BEAT FX: maneja el tape echo
    if (ch == 4 || ch == 5)
    {
        switch (d1)
        {
            case 0x47: case 0x43:
            {
                // tocar y soltar rápido = envío fijo on/off; mantener = throw mientras se mantiene
                const bool spring = d1 == 0x43;
                auto& hold = spring ? engine.revThrow : engine.echoThrow;
                const int k = spring ? 1 : 0;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (press) { fxOnOffPressMs[k] = now; hold.set (Hold::Midi2, true); }
                else
                {
                    hold.set (Hold::Midi2, false);
                    if (now - fxOnOffPressMs[k] < 300.0) p.toggle (spring ? P::RevLatch : P::EchoLatch);
                }
                break;
            }
            case 0x63: case 0x64: if (press) engine.killEcho(); break;
            case 0x4A: if (press) engine.stepEchoDivision (-1); break;
            case 0x4B: if (press) engine.stepEchoDivision (1); break;
            case 0x10: p.set (P::EchoSrc, press ? 0.0f : 2.0f); break;   // selector de canal en 1
            case 0x11: p.set (P::EchoSrc, press ? 1.0f : 2.0f); break;   // selector de canal en 2
            default: break;
        }
        return;
    }

    // ---------------- pads (canales 8-11)
    if (ch >= 7 && ch <= 10)
    {
        const int d = (ch - 7) / 2;
        const bool sh = (ch == 8 || ch == 10);
        const int src = d == 0 ? Hold::Midi : Hold::Midi2;
        const int group = d1 & 0xF0, i = d1 & 0x0F;
        if (i > 7) return;

        if (group == 0x00)          // HOT CUE
        {
            if (sh) { if (press) engine.deck (d).pushCommand ({ DeckCmd::HotcueClear, i, 1.0 }); }
            else engine.deck (d).pushCommand ({ DeckCmd::Hotcue, i, press ? 1.0 : 0.0 });
        }
        else if (group == 0x40)     // KEYBOARD -> DUB: sirenas, throws, freeze
        {
            if (i < 5) engine.setSiren (i, src, press);
            else if (i == 5) engine.echoThrow.set (src, press);
            else if (i == 6) engine.revThrow.set (src, press);
            else engine.echoFreeze.set (src, press);
        }
        else if (group == 0x10)     // PAD FX1 -> ECHO Y SPRING
        {
            static const A fx[8] = { A::EchoThrow, A::RevThrow, A::EchoFreeze, A::EchoKill,
                                     A::EchoShorter, A::EchoLonger, A::EchoLatchT, A::RevLatchT };
            perform (fx[i], src, press);
        }
        else if (group == 0x20)     // BEAT JUMP -> AISLADOR (divisor de frecuencias)
        {
            if (! press) return;
            if (i < 5) perform ((A) ((int) A::KillT1 + i), src, true);
            else if (i == 5) perform (A::Flat, src, true);
            else if (i == 6) perform (A::SubBoostT, src, true);
            else perform (A::IsoModeNext, src, true);
        }
        else if (group == 0x60)     // BEAT LOOP -> TIEMPO DEL ECHO
        {
            if (! press) return;
            if (i < 7) engine.selectEchoDivision (i); else engine.tap();
        }
        else if (group == 0x30)     // SAMPLER
        {
            if (! press) return;
            const int slot = d * 8 + i;
            if (sh) engine.getSampler().stop (slot); else engine.getSampler().trigger (slot);
        }
    }
}

void Controller::confirmFlx4PadMode (int deck, int modeNote)
{
    flx4PadMode[deck].store (modeNote);
    flx4ModeDirty.store (true);
    // la luz se manda en el hilo de mensajes (ahí vive la salida MIDI)
    runOnMessageThread ([this, deck, modeNote]
    {
        ledCache.erase (((0x90 + deck) << 8) | modeNote);   // reenviar aunque ya se haya mandado antes
        if (flx4Out != nullptr) updateLeds();
    });
}

// ---------------------------------------------------------------------------
// KORG nanoPAD2 de fábrica: 4 escenas de 16 pads (notas 36-51, 52-67, 68-83, 84-99),
// pad XY: X = pitch bend, Y = CC 1.
void Controller::handleNano (const juce::MidiMessage& m)
{
    auto& p = engine.params;
    const int src = Hold::Midi2;

    auto axis = [&] (int idx, float v, bool isRest)
    {
        // al soltar el dedo el pad vuelve al centro / cero: ese salto se ignora
        if (isRest && nanoLast[idx] >= 0.0f && std::abs (v - nanoLast[idx]) > 0.3f) return;
        nanoLast[idx] = v;
        P target = idx == 0 ? P::EchoTime : P::EchoFb;
        if (engine.isSirenHeld()) target = idx == 0 ? P::SirenPitch : P::SirenRate;
        p.setNorm (target, v);
    };

    if (m.isPitchWheel()) { const int v = m.getPitchWheelValue(); axis (0, v / 16383.0f, v == 8192); return; }
    if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 1) axis (1, v / 127.0f, v == 0);
        return;
    }
    if (! m.isNoteOnOrOff()) return;

    const bool press = m.isNoteOn() && m.getVelocity() > 0;
    const int note = m.getNoteNumber();
    if (note < 36 || note > 99) return;
    const int scene = (note - 36) / 16, i = (note - 36) % 16;

    static const A scene1[16] = {   // DUB / ECHO (igual que Dub Echo)
        A::EchoThrow, A::Tap, A::EchoKill, A::EchoFreeze, A::EchoLatchT,
        A::Div2, A::Div3, A::Div4, A::Div5, A::Div7, A::EchoShorter, A::EchoLonger,
        A::Siren1, A::Siren2, A::Siren3, A::Siren4 };
    static const A scene2[16] = {   // SPRING, AISLADOR Y ENVÍOS
        A::RevThrow, A::RevLatchT, A::BothThrow, A::Siren5,
        A::KillT1, A::KillT2, A::KillT3, A::KillT4, A::KillT5, A::Flat, A::SubBoostT, A::IsoModeNext,
        A::SrcCh1, A::SrcCh2, A::SrcBoth, A::SmpStop };
    static const A scene4[16] = {   // DECKS
        A::Play1, A::Cue1, A::Play2, A::Cue2,
        A::Hot1A, A::Hot1B, A::Hot1C, A::Hot1D, A::Hot2A, A::Hot2B, A::Hot2C, A::Hot2D,
        A::Reloop1, A::Reloop2, A::Load1, A::Load2 };

    switch (scene)
    {
        case 0: perform (scene1[i], src, press); break;
        case 1: perform (scene2[i], src, press); break;
        case 2: perform ((A) ((int) A::Smp1 + i), src, press); break;   // SAMPLES 1-16
        case 3: perform (scene4[i], src, press); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
void Controller::timerCallback()
{
    ++ticks;

    // terminar el aprendizaje de un eje / perilla
    {
        const juce::SpinLock::ScopedLockType sl (mapLock);
        const int target = learnTarget.load();
        if (target >= 0 && ! learnCands.empty() && juce::Time::getMillisecondCounterHiRes() - learnStart > 500.0)
        {
            const Cand* best = &learnCands.front();
            for (auto& c : learnCands) if (c.maxV - c.minV > best->maxV - best->minV) best = &c;
            for (auto it = learned.begin(); it != learned.end();)
                it = (it->second == target) ? learned.erase (it) : std::next (it);
            learned[best->key] = target;
            learnCands.clear();
            learnTarget.store (-1);
        }
    }

    // conexión con la FLX4 (entrada habilitada y salida para las luces)
    if (ticks % 40 == 1)
    {
        bool inOk = false;
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (isFlx4Name (d.name) && devices.isMidiInputDeviceEnabled (d.identifier)) inOk = true;
        flx4In.store (inOk);
        bool nanoOk = false;
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (isNanoName (d.name) && devices.isMidiInputDeviceEnabled (d.identifier)) nanoOk = true;
        nanoIn.store (nanoOk);

        juce::String outId;
        for (auto& d : juce::MidiOutput::getAvailableDevices())
            if (isFlx4Name (d.name)) outId = d.identifier;
        if (outId.isEmpty()) { flx4Out.reset(); ledCache.clear(); }
        else if (flx4Out == nullptr || flx4Out->getIdentifier() != outId)
        {
            flx4Out = juce::MidiOutput::openDevice (outId);
            ledCache.clear();
        }
    }

    if (flx4Out != nullptr)
    {
        if (ticks % 5 == 0)
        {
            // mensaje de "sigo acá" que espera la FLX4 (sin esto, a veces vuelve al modo demo)
            static const juce::uint8 keepAlive[] = { 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02 };
            flx4Out->sendMessageNow (juce::MidiMessage::createSysExMessage (keepAlive, (int) sizeof (keepAlive)));
        }
        updateLeds();
    }
}

void Controller::sendLed (int status, int note, int value)
{
    const int k = (status << 8) | note;
    auto it = ledCache.find (k);
    if (it != ledCache.end() && it->second == value) return;
    ledCache[k] = value;
    flx4Out->sendMessageNow (juce::MidiMessage (status, note, value));
}

void Controller::updateLeds()
{
    auto& p = engine.params;
    const bool blink = (ticks / 5) % 2 == 0;
    for (int d = 0; d < 2; ++d)
    {
        auto& dk = engine.deck (d);
        const bool loaded = dk.getTrack() != nullptr;
        const int st = 0x90 + d;
        sendLed (st, 0x0B, dk.isPlaying() ? 0x7F : (loaded && blink ? 0x7F : 0));
        sendLed (st, 0x0C, loaded && ! dk.isPlaying() ? 0x7F : 0);
        sendLed (st, 0x54, p.on (d == 0 ? P::CueA : P::CueB) ? 0x7F : 0);
        sendLed (st, 0x10, dk.isLoopActive() ? 0x7F : 0);
        sendLed (st, 0x11, dk.isLoopActive() ? 0x7F : 0);

        // luz del botón de modo activo (así la FLX4 cambia de modo)
        const int mode = flx4PadMode[d].load();
        sendLed (st, mode, 0x7F);

        const int ps = 0x97 + 2 * d;
        const int src = d == 0 ? Hold::Midi : Hold::Midi2;
        juce::ignoreUnused (src);
        for (int i = 0; i < 8; ++i)
        {
            sendLed (ps, 0x00 + i, dk.getHotcueSeconds (i) >= 0.0 ? 0x7F : 0);

            bool fx = false;
            switch (i)
            {
                case 0: fx = engine.echoThrow.held(); break;
                case 1: fx = engine.revThrow.held(); break;
                case 2: fx = engine.echoFreeze.held(); break;
                case 4: fx = engine.echoShorter.held(); break;
                case 5: fx = engine.echoLonger.held(); break;
                case 6: fx = p.on (P::EchoLatch); break;
                case 7: fx = p.on (P::RevLatch); break;
                default: break;
            }
            sendLed (ps, 0x10 + i, fx ? 0x7F : 0);

            int dub = 0;
            if (i < 5) dub = engine.getSirenPlaying() == i ? 0x7F : 0;
            else if (i == 5) dub = engine.echoThrow.held() ? 0x7F : 0;
            else if (i == 6) dub = engine.revThrow.held() ? 0x7F : 0;
            else dub = engine.echoFreeze.held() ? 0x7F : 0;
            sendLed (ps, 0x40 + i, dub);

            int iso = 0;
            if (i < 5) iso = p.on ((P) ((int) P::Kill1 + i)) ? 0x7F : 0;
            else if (i == 6) iso = p.on (P::SubBoost) ? 0x7F : 0;
            else if (i == 7) iso = p.choice (P::IsoMode) == 2 ? 0x7F : 0;
            sendLed (ps, 0x20 + i, iso);

            sendLed (ps, 0x60 + i, (i < 7 && engine.getEchoDivision() == i && engine.echoTimeIsSynced()) ? 0x7F : 0);

            const int slot = d * 8 + i;
            const bool playing = engine.getSampler().isPlaying (slot);
            const bool has = engine.getSampler().get (slot) != nullptr;
            sendLed (ps, 0x30 + i, playing ? (blink ? 0x7F : 0) : (has ? 0x7F : 0));
        }

        // vúmetro del canal
        const float db = juce::Decibels::gainToDecibels (engine.getChannelLevel (d), -60.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f);
        sendLed (0xB0 + d, 0x02, juce::roundToInt (n * n * 127.0f));
    }
    sendLed (0x94, 0x47, p.on (P::EchoLatch) ? 0x7F : 0);
    sendLed (0x94, 0x43, p.on (P::RevLatch) ? 0x7F : 0);
}

// ---------------------------------------------------------------------------
juce::String Controller::describeMappingFor (int target) const
{
    const juce::SpinLock::ScopedLockType sl (mapLock);
    juce::StringArray out;
    for (auto& [key, t] : learned)
    {
        if (t != target) continue;
        const int type = key >> 16, ch = (key >> 8) & 0xff, num = key & 0xff;
        out.add ((type == 1 ? "Nota " + juce::String (num) : type == 2 ? "CC " + juce::String (num) : juce::String ("Pitch bend"))
                 + " (canal " + juce::String (ch) + ")");
    }
    return out.joinIntoString (", ");
}

void Controller::clearMappingFor (int target)
{
    const juce::SpinLock::ScopedLockType sl (mapLock);
    for (auto it = learned.begin(); it != learned.end();)
        it = (it->second == target) ? learned.erase (it) : std::next (it);
}

juce::var Controller::mappingsToVar() const
{
    const juce::SpinLock::ScopedLockType sl (mapLock);
    juce::Array<juce::var> arr;
    for (auto& [key, t] : learned)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("midi", key);
        o->setProperty ("target", t >= kActionBase ? juce::String ("a:") + actionInfo ((A) (t - kActionBase)).key
                                                   : juce::String ("p:") + paramInfo ((P) t).key);
        arr.add (juce::var (o));
    }
    return arr;
}

void Controller::mappingsFromVar (const juce::var& v)
{
    const juce::SpinLock::ScopedLockType sl (mapLock);
    learned.clear();
    if (auto* arr = v.getArray())
        for (auto& item : *arr)
        {
            const int key = item["midi"];
            const auto target = item["target"].toString();
            int t = -1;
            if (target.startsWith ("p:")) t = Params::findByKey (target.substring (2));
            else if (target.startsWith ("a:"))
                for (int i = 0; i < kNumActions; ++i)
                    if (target.substring (2) == actionInfo ((A) i).key) t = kActionBase + i;
            if (t >= 0) learned[key] = t;
        }
}

} // namespace ct
