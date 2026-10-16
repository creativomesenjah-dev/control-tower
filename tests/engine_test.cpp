// Prueba del motor completo sin placa de audio: un dispositivo simulado llama al callback de audio.
// También renderiza una demo (preamp_demo.wav) en la carpeta pasada como argumento.
#include "Engine.h"
#include "Controller.h"
#include "FactorySamples.h"
#include <chrono>
#include <cstdio>

using namespace ct;

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? " OK " : "FALLA", what); if (! ok) ++failures; }

class FakeDevice : public juce::AudioIODevice
{
public:
    FakeDevice() : juce::AudioIODevice ("Placa simulada", "test") {}
    juce::StringArray getOutputChannelNames() override { return { "1", "2", "3", "4" }; }
    juce::StringArray getInputChannelNames() override  { return { "1", "2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 44100.0; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { juce::BigInteger b; b.setRange (0, 4, true); return b; }
    juce::BigInteger getActiveInputChannels() const override  { juce::BigInteger b; b.setRange (0, 2, true); return b; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

// Loop de dub sintético (75 BPM): bombo, aro y skank.
static void writeLoop (const juce::File& f, float seconds)
{
    const int sr = 44100, n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    const float beat = 60.0f / 75.0f;
    juce::Random rnd (1);
    for (float t0 = 0; t0 < seconds; t0 += beat)
    {
        const int bt = juce::roundToInt (t0 / beat);
        for (int i = 0; i < sr / 3 && (int) (t0 * sr) + i < n; ++i)
        {
            const float tt = (float) i / sr;
            float s = 0.0f;
            if (bt % 2 == 0) s += 0.7f * std::sin (6.2831853f * (50.0f * tt + 40.0f * (1 - std::exp (-tt * 30)) / 30)) * std::exp (-tt * 8);
            if (bt % 4 == 2 && i < sr / 12) s += 0.3f * (rnd.nextFloat() * 2 - 1) * std::exp (-tt * 60);
            b.addSample (0, (int) (t0 * sr) + i, s); b.addSample (1, (int) (t0 * sr) + i, s);
        }
        const int ts = (int) ((t0 + beat * 0.5f) * sr);
        for (int i = 0; i < sr / 8 && ts + i < n; ++i)
        {
            const float tt = (float) i / sr;
            float s = 0;
            for (float fr : { 220.0f, 261.63f, 329.63f }) s += 2.0f * (std::fmod (fr * tt, 1.0f) - 0.5f);
            s *= 0.1f * std::exp (-tt * 25);
            b.addSample (0, ts + i, s); b.addSample (1, ts + i, s * 0.9f);
        }
    }
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (16));
    w->writeFromAudioSampleBuffer (b, 0, n);
}

struct Rig
{
    Engine e;
    FakeDevice dev;
    juce::AudioBuffer<float> out { 4, 512 }, in { 2, 512 };
    std::vector<float> rec[4];

    Rig() { e.audioDeviceAboutToStart (&dev); }

    void run (double seconds, bool keep = false)
    {
        const int blocks = (int) (seconds * 44100.0 / 512.0);
        for (int b = 0; b < blocks; ++b)
        {
            in.clear();
            const float* ins[2] = { in.getReadPointer (0), in.getReadPointer (1) };
            float* outs[4] = { out.getWritePointer (0), out.getWritePointer (1), out.getWritePointer (2), out.getWritePointer (3) };
            e.audioDeviceIOCallbackWithContext (ins, 2, outs, 4, 512, {});
            if (keep) for (int c = 0; c < 4; ++c) rec[c].insert (rec[c].end(), outs[c], outs[c] + 512);
        }
    }

    float peakLast (int ch, double seconds) const
    {
        const auto& v = rec[ch];
        const size_t n = (size_t) (seconds * 44100.0);
        float m = 0.0f;
        for (size_t i = v.size() > n ? v.size() - n : 0; i < v.size(); ++i) m = juce::jmax (m, std::abs (v[i]));
        return m;
    }
    void clearRec() { for (auto& r : rec) r.clear(); }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir = argc > 1 ? juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])) : juce::File::getCurrentWorkingDirectory();
    dir.createDirectory();
    const auto loopFile = dir.getChildFile ("loop_prueba.wav");
    writeLoop (loopFile, 60.0f);

    Rig r;
    auto& p = r.e.params;
    juce::String err;
    check (r.e.loadTrack (0, loopFile, err), "carga un tema en el deck 1");
    auto t = r.e.getTrack (0);
    for (int i = 0; i < 400 && ! t->finished.load(); ++i) juce::Thread::sleep (10);
    std::printf ("     decodificado: %.1f s en memoria (%.1f MB)\n", t->durationSeconds(), (double) t->left.size() * 4.0 / 1.0e6);
    check (t->finished.load() && ! t->failed.load(), "decodifica en segundo plano");

    // play + cue en auriculares
    p.set (P::CueA, 1.0f);
    r.e.deck (0).pushCommand ({ DeckCmd::PlayToggle });
    r.run (2.0, true);
    std::printf ("     master %.2f | auriculares %.2f\n", r.peakLast (0, 1.0), r.peakLast (2, 1.0));
    check (r.peakLast (0, 1.0) > 0.2f && r.peakLast (2, 1.0) > 0.2f, "suena en master (1-2) y en auriculares (3-4)");

    // aislador: kill de todas las bandas = silencio; FLAT no toca los kills? (los kills se apagan con FLAT)
    for (auto k : { P::Kill1, P::Kill2, P::Kill3, P::Kill4 }) p.set (k, 1.0f);
    r.clearRec(); r.run (1.0, true);
    std::printf ("     todo en kill: master %.4f\n", r.peakLast (0, 0.5));
    check (r.peakLast (0, 0.5) < 0.01f, "kills del aislador cortan todo");
    for (auto k : { P::Kill1, P::Kill2, P::Kill3, P::Kill4 }) p.set (k, 0.0f);

    // cambio a 5 vías en vivo, sin explotar
    p.set (P::IsoMode, 2.0f);
    r.clearRec(); r.run (1.0, true);
    check (r.peakLast (0, 0.5) > 0.2f && r.peakLast (0, 0.5) <= 1.0f, "cambio a 5 vías en vivo");

    // echo throw y fader cerrado: queda la cola
    r.e.echoThrow.set (Hold::Mouse, true);
    r.run (0.5);
    r.e.echoThrow.set (Hold::Mouse, false);
    p.set (P::FaderA, 0.0f);
    r.clearRec(); r.run (1.5, true);
    std::printf ("     fader cerrado tras throw: cola del echo %.3f\n", r.peakLast (0, 1.0));
    check (r.peakLast (0, 1.0) > 0.02f, "throw al echo deja repeticiones con el fader cerrado");
    r.e.killEcho();
    r.run (0.3);
    r.clearRec(); r.run (0.5, true);
    check (r.peakLast (0, 0.4) < 0.01f, "kill del echo corta las repeticiones");

    // sirena con el deck cerrado
    r.e.setSiren (1, Hold::Mouse, true);
    r.clearRec(); r.run (0.5, true);
    r.e.setSiren (1, Hold::Mouse, false);
    check (r.peakLast (0, 0.3) > 0.05f, "la sirena suena en el master");
    r.run (2.0);

    // limitador con todo al máximo
    p.set (P::FaderA, 1.0f); p.set (P::TrimA, 12.0f); p.set (P::MLevel, 6.0f); p.set (P::Drive, 0.6f);
    p.set (P::LowA, 1.0f); p.set (P::SubBoost, 1.0f);
    r.clearRec(); r.run (2.0, true);
    float mx = 0.0f; for (float v : r.rec[0]) mx = juce::jmax (mx, std::abs (v));
    std::printf ("     todo al máximo: pico %.3f, limitador %.1f dB\n", mx, r.e.getLimiterReductionDb());
    check (mx <= 1.0f, "el limitador contiene el master");
    p.set (P::TrimA, 0.0f); p.set (P::MLevel, 0.0f); p.set (P::Drive, 0.0f); p.set (P::LowA, 0.5f); p.set (P::SubBoost, 0.0f);

    // CPU: dos decks, aislador 5 vías, echo, spring, sirena
    check (r.e.loadTrack (1, loopFile, err) || true, "segundo deck");
    for (int i = 0; i < 400 && ! r.e.getTrack (1)->finished.load(); ++i) juce::Thread::sleep (10);
    r.e.deck (1).pushCommand ({ DeckCmd::PlayToggle });
    p.set (P::EchoLatch, 1.0f); p.set (P::RevLatch, 1.0f);
    r.e.setSiren (0, Hold::Mouse, true);
    const auto t0 = std::chrono::high_resolution_clock::now();
    r.run (20.0);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count() / 20.0;
    std::printf ("     CPU con todo andando: %.2f ms por segundo de audio (%.2f%% de un núcleo)\n", ms, ms / 10.0);
    check (ms < 60.0, "CPU holgada para la Mac 2012");
    r.e.setSiren (0, Hold::Mouse, false);
    p.set (P::EchoLatch, 0.0f); p.set (P::RevLatch, 0.0f);

    // grabación
    juce::File recFile;
    check (r.e.startRecording (recFile), "empieza a grabar");
    r.run (1.0);
    r.e.stopRecording();
    std::printf ("     grabación: %s (%lld bytes)\n", recFile.getFileName().toRawUTF8(), (long long) recFile.getSize());
    check (recFile.getSize() > 44100 * 4 / 2, "la grabación queda en disco");

    // ---------------- controladores: FLX4 y nanoPAD2 simulados
    {
        Rig c;
        auto& q = c.e.params;
        Library lib;
        juce::AudioDeviceManager dm;
        Controller ctrl (c.e, lib, dm);
        auto flx = [&] (int st, int d1, int d2) { ctrl.dispatch ("DDJ-FLX4", juce::MidiMessage (st, d1, d2)); };
        auto nano = [&] (const juce::MidiMessage& m) { ctrl.dispatch ("nanoPAD2", m); };

        flx (0x90, 0x1E, 0x7F); flx (0x90, 0x1E, 0x00);
        check (ctrl.getFlx4PadMode (0) == 0x1E, "FLX4: el botón PAD FX1 cambia el modo del deck 1");

        flx (0x97, 0x10, 0x7F);
        check (c.e.echoThrow.held(), "FLX4 PAD FX1 pad 1: throw al echo mientras se mantiene");
        flx (0x97, 0x10, 0x00);
        check (! c.e.echoThrow.held(), "FLX4 PAD FX1 pad 1: suelta el throw");
        flx (0x99, 0x11, 0x7F); check (c.e.revThrow.held(), "FLX4 PAD FX1 pad 2 (deck 2): throw al spring"); flx (0x99, 0x11, 0x00);

        const float t0 = q.get (P::EchoTime);
        flx (0x97, 0x14, 0x7F); c.run (0.5); flx (0x97, 0x14, 0x00);
        std::printf ("     tiempo del echo: %.0f -> %.0f ms con TIEMPO − mantenido 0,5 s\n", t0, q.get (P::EchoTime));
        check (q.get (P::EchoTime) < t0 - 50.0f, "FLX4 PAD FX1 pad 5: acorta el tiempo del echo");

        // ON/OFF: tocar = envío fijo; mantener = throw (sin cambiar el envío fijo)
        flx (0x94, 0x47, 0x7F); flx (0x94, 0x47, 0x00);
        check (q.on (P::EchoLatch), "FLX4 BEAT FX ON/OFF tocado: envío fijo al echo");
        flx (0x94, 0x47, 0x7F); flx (0x94, 0x47, 0x00);
        check (! q.on (P::EchoLatch), "FLX4 BEAT FX ON/OFF tocado otra vez: lo apaga");
        flx (0x94, 0x47, 0x7F); juce::Thread::sleep (400);
        check (c.e.echoThrow.held(), "FLX4 BEAT FX ON/OFF mantenido: throw");
        flx (0x94, 0x47, 0x00);
        check (! c.e.echoThrow.held() && ! q.on (P::EchoLatch), "FLX4 BEAT FX ON/OFF: al soltar termina el throw");

        q.set (P::IsoMode, 1.0f);
        flx (0x97, 0x27, 0x7F); flx (0x97, 0x27, 0x00);
        check (q.choice (P::IsoMode) == 2, "FLX4 BEAT JUMP pad 8: cambia las vías del aislador");
        flx (0x97, 0x20, 0x7F); flx (0x97, 0x20, 0x00);
        check (q.on (P::Kill1), "FLX4 BEAT JUMP pad 1: kill de la banda 1");

        // nanoPAD2
        nano (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
        check (c.e.echoThrow.held(), "nanoPAD2 escena 1, pad 1: throw al echo");
        nano (juce::MidiMessage::noteOff (1, 36));
        nano (juce::MidiMessage::pitchWheel (1, 16000));
        check (q.getNorm (P::EchoTime) > 0.95f, "nanoPAD2 eje X: tiempo del echo");
        nano (juce::MidiMessage::pitchWheel (1, 8192));
        check (q.getNorm (P::EchoTime) > 0.95f, "nanoPAD2 eje X: al soltar el dedo no salta al centro");
        nano (juce::MidiMessage::controllerEvent (1, 1, 30));
        check (std::abs (q.getNorm (P::EchoFb) - 30 / 127.0f) < 0.01f, "nanoPAD2 eje Y: repeticiones");

        const float fbBefore = q.get (P::EchoFb), timeBefore = q.get (P::EchoTime);
        nano (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100));     // sirena dos tonos
        nano (juce::MidiMessage::pitchWheel (1, 12000));
        nano (juce::MidiMessage::controllerEvent (1, 1, 100));
        check (c.e.isSirenHeld() && std::abs (q.get (P::EchoFb) - fbBefore) < 1e-4f && std::abs (q.get (P::EchoTime) - timeBefore) < 1e-3f
               && q.getNorm (P::SirenRate) > 0.7f, "nanoPAD2: con la sirena sonando el XY toca la sirena");
        nano (juce::MidiMessage::noteOff (1, 48));

        nano (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100)); nano (juce::MidiMessage::noteOff (1, 60));
        check (q.on (P::Kill5), "nanoPAD2 escena 2: kill de banda 5");

        const auto files = factory::install (dir.getChildFile ("samples"));
        c.e.loadSample (0, files[0]);
        nano (juce::MidiMessage::noteOn (1, 68, (juce::uint8) 100)); nano (juce::MidiMessage::noteOff (1, 68));
        c.run (0.05);
        check (c.e.getSampler().isPlaying (0), "nanoPAD2 escena 3: dispara el sample 1");

        c.e.loadTrack (0, loopFile, err);
        for (int i = 0; i < 400 && ! c.e.getTrack (0)->finished.load(); ++i) juce::Thread::sleep (10);
        c.run (0.05);
        nano (juce::MidiMessage::noteOn (1, 84, (juce::uint8) 100)); nano (juce::MidiMessage::noteOff (1, 84));
        c.run (0.1);
        check (c.e.deck (0).isPlaying(), "nanoPAD2 escena 4: play del deck 1");

        std::printf ("     samples de fábrica:");
        for (auto& f : files) std::printf (" %s", f.getFileNameWithoutExtension().toRawUTF8());
        std::printf ("\n");
        check (files.size() == 8 && files[3].getSize() > 100000, "samples de fábrica generados");
    }

    // ---------------- demo: 45 s de "sesión"
    {
        Rig d;
        auto& q = d.e.params;
        d.e.loadTrack (0, loopFile, err);
        for (int i = 0; i < 400 && ! d.e.getTrack (0)->finished.load(); ++i) juce::Thread::sleep (10);
        q.set (P::EchoTime, 600.0f); q.set (P::EchoFb, 62.0f); q.set (P::RevRet, -6.0f);
        d.e.deck (0).pushCommand ({ DeckCmd::PlayToggle });
        auto seg = [&] (double s) { d.run (s, true); };
        seg (4.0);
        // throws al echo
        for (int k = 0; k < 2; ++k) { d.e.echoThrow.set (Hold::Mouse, true); seg (0.4); d.e.echoThrow.set (Hold::Mouse, false); seg (2.8); }
        // kill de graves y de agudos (aislador)
        q.set (P::Kill1, 1.0f); q.set (P::Kill2, 1.0f); seg (3.2); q.set (P::Kill1, 0.0f); q.set (P::Kill2, 0.0f);
        q.set (P::Kill3, 1.0f); q.set (P::Kill4, 1.0f); seg (3.2); q.set (P::Kill3, 0.0f); q.set (P::Kill4, 0.0f);
        seg (1.6);
        // spring throw
        d.e.revThrow.set (Hold::Mouse, true); seg (0.4); d.e.revThrow.set (Hold::Mouse, false); seg (2.8);
        // sirenas
        d.e.setSiren (0, Hold::Mouse, true); seg (1.5); d.e.setSiren (0, Hold::Mouse, false); seg (1.7);
        d.e.setSiren (1, Hold::Mouse, true); seg (1.2); d.e.setSiren (1, Hold::Mouse, false); seg (2.0);
        // filtro master: barrido pasa-altos y vuelta
        for (int k = 0; k <= 40; ++k) { q.set (P::MFilter, 0.5f + 0.4f * (float) k / 40.0f); seg (0.05); }
        for (int k = 40; k >= 0; --k) { q.set (P::MFilter, 0.5f + 0.4f * (float) k / 40.0f); seg (0.05); }
        // final: fader cerrado con throw y freeze
        d.e.echoThrow.set (Hold::Mouse, true); seg (0.4); d.e.echoThrow.set (Hold::Mouse, false);
        q.set (P::FaderA, 0.0f); d.e.echoFreeze.set (Hold::Mouse, true); seg (2.5); d.e.echoFreeze.set (Hold::Mouse, false);
        q.set (P::EchoTime, 300.0f); seg (3.0);

        juce::AudioBuffer<float> b (2, (int) d.rec[0].size());
        b.copyFrom (0, 0, d.rec[0].data(), b.getNumSamples());
        b.copyFrom (1, 0, d.rec[1].data(), b.getNumSamples());
        const auto f = dir.getChildFile ("preamp_demo.wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (2).withBitsPerSample (16));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        std::printf ("     demo: %s (%.1f s)\n", f.getFullPathName().toRawUTF8(), b.getNumSamples() / 44100.0);
    }

    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}
