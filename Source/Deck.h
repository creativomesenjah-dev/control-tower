// Deck.h — bandeja de reproducción. Sin dependencias de JUCE.
//
// El tema se decodifica en segundo plano a memoria en 16 bits (≈10 MB por minuto estéreo):
// así los saltos (cue, hot cues, loops, scratch) son instantáneos y sin cortes, y la RAM
// sólo guarda los 2 temas cargados, nunca la biblioteca.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ct
{

struct TrackAudio
{
    std::vector<int16_t> left, right;
    // forma de onda: un valor 0..255 cada kPeakFrames cuadros (total y sólo graves)
    static constexpr int kPeakFrames = 256;
    std::vector<uint8_t> peaks, peaksLow;
    float sampleRate = 44100.0f;
    std::atomic<int64_t> lengthFrames { 0 };
    std::atomic<bool> cancel { false };   // el deck lo reemplazó: dejar de decodificar
    std::atomic<int64_t> decodedFrames { 0 };
    std::atomic<bool> finished { false }, failed { false };
    // grilla de beats (la calcula el cargador al terminar de decodificar; 0 = todavía no / no se pudo)
    std::atomic<double> bpm { 0.0 }, firstBeat { 0.0 };
    std::string title, path;

    void allocate (int64_t frames, int channels, float sr)
    {
        juceFreeSafeResize (frames);
        sampleRate = sr;
        lengthFrames.store (frames);
        (void) channels;
    }

    float durationSeconds() const { return (float) lengthFrames.load() / sampleRate; }

private:
    void juceFreeSafeResize (int64_t frames)
    {
        left.assign ((size_t) frames, 0);
        right.assign ((size_t) frames, 0);
        const size_t np = (size_t) (frames / kPeakFrames + 1);
        peaks.assign (np, 0);
        peaksLow.assign (np, 0);
    }
};

enum class DeckCmd : int
{
    PlayToggle, Cue, Hotcue, HotcueClear, Seek, SeekFrac, JumpStart,
    LoopIn, LoopOut, Reloop, LoopHalve, LoopDouble,
    JogTouch, JogScratch, JogBend, JogSearch,
    ReloopStop, BeatJump, LoopAdjustIn, LoopAdjustOut, BpmScale
};

struct DeckCommand
{
    DeckCmd type;
    int index = 0;
    double value = 0.0;   // press = 1 / release = 0, segundos, ticks, etc.
};

class Deck
{
public:
    static constexpr int kNumHotcues = 8;
    static constexpr double kTicksPerSecond = 400.0;  // plato FLX4: 720 pasos por vuelta a 33⅓ rpm

    Deck() { for (auto& h : hotcueSec) h.store (-1.0); }

    void prepare (float deviceSampleRate, int maxBlock)
    {
        deviceSr = deviceSampleRate;
        (void) maxBlock;
        velocity = 0.0;
    }

    // Lo llama el hilo de mensajes; el hilo de audio lo toma al principio del próximo bloque.
    void setTrack (TrackAudio* t) { pendingTrack.store (t); trackChangePending.store (true); }
    TrackAudio* getTrack() const  { return track.load(); }

    // pitch: -1..1 del fader de tempo; range: 0.08, 0.16, 0.5
    void setPitch (float pitch, float range) { pitchAmount.store (pitch); pitchRange.store (range); }
    float getRate() const { return 1.0f + pitchAmount.load() * pitchRange.load(); }
    void setQuantize (bool q) { quantize.store (q); }

    void pushCommand (const DeckCommand& c)
    {
        std::lock_guard<std::mutex> lock (cmdMutex);
        const int next = (cmdWrite + 1) % kQueue;
        if (next == cmdRead) return;   // cola llena: se descarta (no debería pasar)
        queue[(size_t) cmdWrite] = c;
        cmdWrite = next;
    }

    // --- estado para la interfaz y las luces
    bool   isPlaying() const              { return playingState.load(); }
    double getPositionSeconds() const     { return positionSec.load(); }
    double getCueSeconds() const          { return cueSec.load(); }
    double getHotcueSeconds (int i) const { return hotcueSec[(size_t) i].load(); }
    bool   isLoopActive() const           { return loopActiveState.load(); }
    double getLoopInSeconds() const       { return loopInSec.load(); }
    double getLoopOutSeconds() const      { return loopOutSec.load(); }
    bool   isJogTouched() const           { return touched; }
    // 0 = nada, 1 = ajustando el inicio con el plato, 2 = ajustando el final
    int    getLoopAdjust() const          { return adjustState.load(); }
    double getLoopBeats() const           { return loopBeats.load(); }
    double getBpm() const                 { TrackAudio* t = track.load(); return t != nullptr ? t->bpm.load() : 0.0; }

    // Escribe el audio del deck (estéreo) en L/R. Llamar desde el hilo de audio.
    void process (float* L, float* R, int n)
    {
        if (trackChangePending.exchange (false))
            loadPending();

        drainCommands();

        TrackAudio* t = track.load();
        // LOOP IN mantenido medio segundo: loop automático de 4 beats desde el punto marcado
        if (t != nullptr && loopInHeld && ! autoLoopDone)
        {
            loopInHeldSec += (double) n / deviceSr;
            if (loopInHeldSec >= 0.5 && loopIn >= 0.0)
            {
                setLoop (loopIn, loopIn + 4.0 * beatFrames());
                autoLoopDone = true;
            }
        }
        if (t == nullptr)
        {
            std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
            publish();
            return;
        }

        const double ratio = t->sampleRate / deviceSr;   // muestras del tema por muestra de salida
        const double blockSec = (double) n / deviceSr;
        const double rate = (double) getRate();

        // --- velocidad objetivo
        double target;
        double smoothing;
        if (touched)
        {
            const double measured = scratchTicks / (kTicksPerSecond * blockSec);   // 1.0 = velocidad normal
            scratchTicks = 0.0;
            scratchVel += (measured - scratchVel) * 0.55;
            target = scratchVel;
            smoothing = 0.6;
        }
        else
        {
            target = playing ? rate + bend : 0.0;
            smoothing = playing ? 0.5 : 0.65;    // arranque y frenado rápidos, como un CDJ
        }
        bend *= 0.7;
        if (std::abs (bend) < 1.0e-4) bend = 0.0;

        const double v0 = velocity;
        velocity += (target - velocity) * smoothing;
        if (std::abs (velocity) < 1.0e-5 && target == 0.0) velocity = 0.0;
        const double v1 = velocity;

        const int64_t decoded = t->decodedFrames.load (std::memory_order_acquire);
        const int16_t* dl = t->left.data();
        const int16_t* dr = t->right.data();
        const double len = (double) t->lengthFrames.load();
        constexpr float k16 = 1.0f / 32768.0f;

        for (int i = 0; i < n; ++i)
        {
            const double v = (v0 + (v1 - v0) * (double) i / (double) n) * ratio;

            // lectura con interpolación Hermite
            const double fl = std::floor (pos);
            const auto idx = (int64_t) fl;
            const float fr = (float) (pos - fl);
            float l = 0.0f, r = 0.0f;
            if (idx >= 1 && idx + 2 < decoded)
            {
                l = hermite (dl, idx, fr) * k16;
                r = hermite (dr, idx, fr) * k16;
            }
            L[i] = l; R[i] = r;

            pos += v;
            if (loopActive && v > 0.0 && pos >= loopOut)
                pos = loopIn + std::fmod (pos - loopOut, loopOut - loopIn);
            if (pos < 0.0) pos = 0.0;
            if (pos >= len)
            {
                pos = len;
                if (t->finished.load()) { playing = false; cuePreview = false; hotPreview = -1; }
            }
        }

        publish();
    }

private:
    static constexpr int kQueue = 256;

    static inline float hermite (const int16_t* d, int64_t i, float t)
    {
        const float ym1 = d[i - 1], y0 = d[i], y1 = d[i + 1], y2 = d[i + 2];
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    double sr() const { TrackAudio* t = track.load(); return t != nullptr ? (double) t->sampleRate : 44100.0; }

    void loadPending()
    {
        track.store (pendingTrack.load());
        pos = 0.0; velocity = 0.0; bend = 0.0;
        playing = false; cuePreview = false; hotPreview = -1;
        cuePoint = 0.0;
        hotcues.fill (-1.0);
        loopIn = loopOut = -1.0; loopActive = false;
        loopInHeld = false; adjust = 0;
        scratchTicks = 0.0; scratchVel = 0.0;
    }

    // --- grilla de beats
    double beatFrames() const
    {
        TrackAudio* t = track.load();
        const double b = t != nullptr ? t->bpm.load() : 0.0;
        return 60.0 * sr() / (b > 1.0 ? b : 75.0);     // sin BPM detectado: 75 (tempo típico de dub)
    }
    double snap (double x) const
    {
        TrackAudio* t = track.load();
        if (! quantize.load() || t == nullptr || t->bpm.load() <= 1.0) return x;
        const double b = beatFrames(), f = t->firstBeat.load();
        double y = f + std::round ((x - f) / b) * b;
        if (y < 0.0) y += b;                                   // al principio del tema: el primer beat
        const double len = (double) t->lengthFrames.load();
        if (y > len) y -= b;
        return std::max (0.0, y);
    }
    void setLoop (double in, double out)
    {
        TrackAudio* t = track.load();
        const double len = t != nullptr ? (double) t->lengthFrames.load() : 0.0;
        in = std::clamp (in, 0.0, len);
        out = std::clamp (out, in + 0.005 * sr(), std::max (len, in + 0.005 * sr()));
        loopIn = in; loopOut = out; loopActive = true;
        if (pos >= loopOut) pos = loopIn + std::fmod (pos - loopIn, loopOut - loopIn);
    }

    void drainCommands()
    {
        std::unique_lock<std::mutex> lock (cmdMutex, std::try_to_lock);
        if (! lock.owns_lock()) return;
        while (cmdRead != cmdWrite)
        {
            const DeckCommand c = queue[(size_t) cmdRead];
            cmdRead = (cmdRead + 1) % kQueue;
            handle (c);
        }
    }

    void handle (const DeckCommand& c)
    {
        TrackAudio* t = track.load();
        if (t == nullptr) return;
        const double s = sr();
        const bool press = c.value > 0.5;

        switch (c.type)
        {
            case DeckCmd::PlayToggle:
                if (cuePreview || hotPreview >= 0) { cuePreview = false; hotPreview = -1; playing = true; }
                else playing = ! playing;
                break;

            case DeckCmd::Cue:
                if (press)
                {
                    if (playing && ! cuePreview) { pos = cuePoint; playing = false; velocity = 0.0; }
                    else
                    {
                        if (std::abs (pos - cuePoint) > 0.01 * s) cuePoint = pos;
                        else pos = cuePoint;
                        playing = true; cuePreview = true;
                    }
                }
                else if (cuePreview)
                {
                    playing = false; cuePreview = false; pos = cuePoint; velocity = 0.0;
                }
                break;

            case DeckCmd::Hotcue:
            {
                const auto i = (size_t) std::clamp (c.index, 0, kNumHotcues - 1);
                if (press)
                {
                    if (hotcues[i] < 0.0) { hotcues[i] = pos; }
                    else
                    {
                        pos = hotcues[i];
                        if (! playing) { playing = true; hotPreview = (int) i; }
                    }
                }
                else if (hotPreview == (int) i)
                {
                    playing = false; hotPreview = -1; pos = hotcues[i]; velocity = 0.0;
                }
                break;
            }

            case DeckCmd::HotcueClear:
                hotcues[(size_t) std::clamp (c.index, 0, kNumHotcues - 1)] = -1.0;
                break;

            case DeckCmd::Seek:      pos = std::clamp (c.value * s, 0.0, (double) t->lengthFrames.load()); break;
            case DeckCmd::SeekFrac:  pos = std::clamp (c.value, 0.0, 1.0) * (double) t->lengthFrames.load(); break;
            case DeckCmd::JumpStart: pos = 0.0; break;

            // ---- loops, como en rekordbox: un loop nuevo siempre reemplaza al anterior
            case DeckCmd::LoopIn:
                if (press)
                {
                    loopIn = snap (pos); loopOut = -1.0; loopActive = false; adjust = 0;
                    loopInHeld = true; loopInHeldSec = 0.0; autoLoopDone = false;
                }
                else loopInHeld = false;
                break;

            case DeckCmd::LoopOut:
                if (press && loopIn >= 0.0 && ! loopActive)
                {
                    double out = snap (pos);
                    if (out <= loopIn + 0.005 * s) out = loopIn + (quantize.load() && t->bpm.load() > 1.0 ? beatFrames() : 0.01 * s);
                    setLoop (loopIn, out);
                }
                break;

            case DeckCmd::Reloop:      // con loop: salir · sin loop: volver al último
                if (! press) break;
                if (loopActive) { loopActive = false; adjust = 0; }
                else if (loopIn >= 0.0 && loopOut > loopIn) { loopActive = true; pos = loopIn; }
                break;

            case DeckCmd::ReloopStop:  // volver al loop y frenar
                if (press && loopIn >= 0.0 && loopOut > loopIn)
                {
                    loopActive = true; pos = loopIn; playing = false; cuePreview = false; hotPreview = -1; velocity = 0.0;
                }
                break;

            case DeckCmd::LoopHalve:   // sólo con un loop andando
                if (press && loopActive)
                {
                    const double l = (loopOut - loopIn) * 0.5;
                    if (l > 0.005 * s) setLoop (loopIn, loopIn + l);
                }
                break;

            case DeckCmd::LoopDouble:
                if (press && loopActive)
                    setLoop (loopIn, std::min ((double) t->lengthFrames.load(), loopIn + (loopOut - loopIn) * 2.0));
                break;

            case DeckCmd::BeatJump:    // value = beats (con signo)
            {
                const double d = c.value * beatFrames();
                pos = std::clamp (pos + d, 0.0, (double) t->lengthFrames.load());
                if (loopActive && (pos < loopIn || pos >= loopOut)) { loopActive = false; adjust = 0; }
                break;
            }

            case DeckCmd::LoopAdjustIn:
                if (press && loopActive) adjust = adjust == 1 ? 0 : 1;
                break;
            case DeckCmd::LoopAdjustOut:
                if (press && loopActive) adjust = adjust == 2 ? 0 : 2;
                break;

            case DeckCmd::BpmScale:    // corrección manual del BPM detectado (x2, /2)
                if (t->bpm.load() > 1.0)
                {
                    const double nb = std::clamp (t->bpm.load() * c.value, 40.0, 250.0);
                    t->bpm.store (nb);
                    t->firstBeat.store (std::fmod (t->firstBeat.load(), 60.0 * s / nb));
                }
                break;

            case DeckCmd::JogTouch:
                touched = press && adjust == 0;
                if (touched) { scratchTicks = 0.0; scratchVel = velocity / std::max (1.0e-6, 1.0); }
                break;

            case DeckCmd::JogScratch:
            case DeckCmd::JogBend:
                if (adjust != 0 && loopActive)
                {
                    // ajuste fino del loop con el plato: 1 ms por paso
                    const double d = c.value * 0.001 * s;
                    if (adjust == 1) loopIn = std::clamp (loopIn + d, 0.0, loopOut - 0.005 * s);
                    else             loopOut = std::clamp (loopOut + d, loopIn + 0.005 * s, (double) t->lengthFrames.load());
                    if (pos >= loopOut || pos < loopIn) pos = loopIn;
                    break;
                }
                if (c.type == DeckCmd::JogScratch && touched) scratchTicks += c.value;
                else bend = std::clamp (bend + c.value * 0.004, -0.5, 0.5);
                break;

            case DeckCmd::JogSearch:
                pos = std::clamp (pos + c.value * 0.2 * s, 0.0, (double) t->lengthFrames.load());
                break;
        }
    }

    void publish()
    {
        const double s = sr();
        positionSec.store (pos / s);
        playingState.store (playing);
        cueSec.store (cuePoint / s);
        for (int i = 0; i < kNumHotcues; ++i)
            hotcueSec[(size_t) i].store (hotcues[(size_t) i] < 0.0 ? -1.0 : hotcues[(size_t) i] / s);
        if (! loopActive) adjust = 0;
        loopActiveState.store (loopActive);
        adjustState.store (adjust);
        loopBeats.store (loopIn >= 0.0 && loopOut > loopIn ? (loopOut - loopIn) / beatFrames() : 0.0);
        loopInSec.store (loopIn < 0.0 ? -1.0 : loopIn / s);
        loopOutSec.store (loopOut < 0.0 ? -1.0 : loopOut / s);
    }

    float deviceSr = 48000.0f;
    std::atomic<TrackAudio*> track { nullptr }, pendingTrack { nullptr };
    std::atomic<bool> trackChangePending { false };
    std::atomic<float> pitchAmount { 0.0f }, pitchRange { 0.08f };

    // estado del hilo de audio
    double pos = 0.0, velocity = 0.0, bend = 0.0, cuePoint = 0.0;
    double loopIn = -1.0, loopOut = -1.0;
    bool loopActive = false, playing = false, cuePreview = false, touched = false;
    bool loopInHeld = false, autoLoopDone = false;
    double loopInHeldSec = 0.0;
    int adjust = 0;
    std::atomic<bool> quantize { true };
    std::atomic<int> adjustState { 0 };
    std::atomic<double> loopBeats { 0.0 };
    int hotPreview = -1;
    double scratchTicks = 0.0, scratchVel = 0.0;
    std::array<double, kNumHotcues> hotcues { -1, -1, -1, -1, -1, -1, -1, -1 };

    // cola de comandos (varios productores: interfaz + MIDI)
    std::mutex cmdMutex;
    std::array<DeckCommand, kQueue> queue {};
    int cmdRead = 0, cmdWrite = 0;

    // estado publicado
    std::atomic<double> positionSec { 0.0 }, cueSec { 0.0 }, loopInSec { -1.0 }, loopOutSec { -1.0 };
    std::array<std::atomic<double>, kNumHotcues> hotcueSec {};
    std::atomic<bool> playingState { false }, loopActiveState { false };
};

} // namespace ct
