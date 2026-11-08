// Pruebas offline del DSP (sin JUCE):
//   c++ -O2 -std=c++17 -ISource tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "Dsp.h"
#include "Deck.h"
#include "SpaceEchoDSP.h"
#include "SirenDSP.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

using namespace ct;
static constexpr float SR = 44100.0f;
static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? " OK " : "FALLA", what); if (! ok) ++failures; }

// respuesta en magnitud (dB) a un seno de frecuencia f
static float magnitudeDb (const std::function<void (float*, float*, int)>& proc, float f, float amp = 1.0f)
{
    const int n = (int) SR;
    std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = amp * std::sin (2.0f * kPi * f * (float) i / SR);
    for (int pos = 0; pos < n; pos += 512) proc (L.data() + pos, R.data() + pos, std::min (512, n - pos));
    float pk = 0.0f;
    for (int i = n / 2; i < n; ++i) pk = std::max (pk, std::abs (L[(size_t) i]));
    return gainToDb (pk / amp);
}

int main()
{
    // 1) Aislador plano con 3, 4 y 5 bandas
    const float x3[] = { 300.0f, 3000.0f };
    const float x4[] = { 100.0f, 400.0f, 2500.0f };
    const float x5[] = { 80.0f, 250.0f, 1000.0f, 4000.0f };
    const float* cfgs[] = { x3, x4, x5 };
    for (int b = 3; b <= 5; ++b)
    {
        float worst = 0.0f;
        for (float f : { 40.0f, 80.0f, 100.0f, 150.0f, 250.0f, 400.0f, 700.0f, 1000.0f, 2500.0f, 4000.0f, 9000.0f, 15000.0f })
        {
            Isolator iso; iso.prepare (SR); iso.configure (b, cfgs[b - 3]);
            worst = std::max (worst, std::abs (magnitudeDb ([&] (float* l, float* r, int n) { iso.process (l, r, n); }, f)));
        }
        std::printf ("     aislador %d bandas: desvío máximo %.2f dB\n", b, worst);
        check (worst < 0.3f, "aislador plano con todas las bandas a 0 dB");
    }

    // 2) Kill de graves en 4 bandas: 40 Hz desaparece, 5 kHz intacto
    {
        Isolator iso; iso.prepare (SR); iso.configure (4, x4);
        iso.setBandGain (0, 0.0f);
        iso.process (nullptr, nullptr, 0);
        const float sub = magnitudeDb ([&] (float* l, float* r, int n) { iso.process (l, r, n); }, 40.0f);
        Isolator iso2; iso2.prepare (SR); iso2.configure (4, x4); iso2.setBandGain (0, 0.0f);
        const float top = magnitudeDb ([&] (float* l, float* r, int n) { iso2.process (l, r, n); }, 6000.0f);
        std::printf ("     kill SUB: 40 Hz %.1f dB, 6 kHz %.2f dB\n", sub, top);
        check (sub < -24.0f && std::abs (top) < 0.3f, "kill de banda corta sólo esa banda");
    }

    // 3) Filtro DJ: centro = bypass, izquierda corta agudos, derecha corta graves
    {
        DjFilter f; f.prepare (SR); f.setPosition (0.5f, 0.2f);
        const float c = magnitudeDb ([&] (float* l, float* r, int n) { f.process (l, r, n); }, 5000.0f);
        DjFilter lp; lp.prepare (SR); lp.setPosition (0.15f, 0.2f);
        const float hi = magnitudeDb ([&] (float* l, float* r, int n) { lp.process (l, r, n); }, 5000.0f);
        DjFilter hp; hp.prepare (SR); hp.setPosition (0.85f, 0.2f);
        const float lo = magnitudeDb ([&] (float* l, float* r, int n) { hp.process (l, r, n); }, 60.0f);
        std::printf ("     filtro: centro %.2f dB | izq a 5 kHz %.1f dB | der a 60 Hz %.1f dB\n", c, hi, lo);
        check (std::abs (c) < 0.05f && hi < -20.0f && lo < -20.0f, "filtro DJ de un pote");
    }

    // 4) Spring: hay cola, decae y es estable
    {
        SpringReverb sp; sp.prepare (SR); sp.setParams (0.5f, 0.6f, 0.5f);
        std::vector<float> in ((size_t) SR * 6, 0.0f), L (in.size(), 0.0f), R (in.size(), 0.0f);
        in[100] = 1.0f;
        for (size_t p = 0; p < in.size(); p += 512) sp.process (in.data() + p, L.data() + p, R.data() + p, (int) std::min<size_t> (512, in.size() - p), 1.0f);
        auto pk = [&] (float a, float b) { float m = 0; for (size_t i = (size_t) (a * SR); i < (size_t) (b * SR); ++i) m = std::max (m, std::abs (L[i])); return m; };
        const float early = pk (0.05f, 0.3f), mid = pk (1.0f, 1.3f), late = pk (5.0f, 6.0f);
        std::printf ("     spring: %.4f -> %.4f -> %.6f\n", early, mid, late);
        check (early > 0.01f && mid < early && late < early * 0.01f, "spring con cola que decae");
    }


    // 4b) Spring: dispersión — bajo los ~4 kHz de transición, los agudos llegan después (chirp del resorte)
    {
        SpringReverb sp; sp.prepare (SR); sp.setParams (0.5f, 0.3f, 0.7f);
        std::vector<float> in ((size_t) SR, 0.0f), L (in.size(), 0.0f), R (in.size(), 0.0f);
        in[0] = 1.0f;
        sp.process (in.data(), L.data(), R.data(), (int) in.size(), 1.0f);
        // energía centrada: graves (banda 200-600) vs agudos (2-3.5 kHz) en la primera vuelta
        auto centroid = [&] (float lo, float hi) -> double
        {
            SVF2 a, b; a.set (std::sqrt (lo * hi), SR, std::sqrt (lo * hi) / (hi - lo));
            double num = 0, den = 0;
            for (size_t i = 0; i < (size_t) (0.03f * SR); ++i)
            {
                float l, bp, h; a.tick (L[i], l, bp, h);
                num += (double) i * bp * bp; den += bp * bp;
            }
            (void) b;
            return den > 0 ? num / den / SR * 1000.0 : 0.0;
        };
        const double tLo = centroid (500.0f, 900.0f), tHi = centroid (2000.0f, 3000.0f);
        std::printf ("     spring: llegada graves %.1f ms, agudos %.1f ms\n", tLo, tHi);
        check (tHi > tLo + 2.0, "spring dispersivo (chirp de resorte)");
    }

    // 4c) Space echo: cada modo de cabezales repite en sus tiempos (1×, 1,96×, 2,92×)
    {
        auto firstPeaks = [&] (int mode)
        {
            SpaceEcho e; e.prepare (SR);
            SpaceEchoParams ep; ep.timeMs = 200.0f; ep.heads = headModeMask (mode); ep.intensity = 0.0f; ep.wowFlutter = 0.0f; ep.drive = 0.0f;
            e.setParams (ep);
            std::vector<float> in ((size_t) SR * 1, 0.0f), out (in.size(), 0.0f);
            for (int i = 0; i < 64; ++i) in[(size_t) i] = 0.5f * std::sin (2.0f * kPi * 1000.0f * (float) i / SR);
            // dejar que el glide de cinta llegue a 200 ms antes del golpe
            std::vector<float> silence ((size_t) SR * 3, 0.0f), dump (silence.size());
            e.process (silence.data(), dump.data(), (int) silence.size());
            e.process (in.data(), out.data(), (int) in.size());
            std::vector<int> peaks;
            for (int w = 0; w < 3; ++w)
            {
                const float t = 200.0f * SpaceEcho::kHeadRatio[w];
                float m = 0; for (int i = (int) ((t - 5) * SR / 1000); i < (int) ((t + 10) * SR / 1000); ++i) m = std::max (m, std::abs (out[(size_t) i]));
                peaks.push_back (m > 0.02f ? 1 : 0);
            }
            return peaks[0] | (peaks[1] << 1) | (peaks[2] << 2);
        };
        bool ok = true;
        for (int m = 0; m < kNumHeadModes; ++m)
        {
            const int got = firstPeaks (m);
            std::printf ("     echo modo %s: cabezales que suenan %d%d%d\n", headModeLabel (m), got & 1, (got >> 1) & 1, (got >> 2) & 1);
            ok = ok && got == headModeMask (m);
        }
        check (ok, "space echo: los 7 modos de cabezales");

        // intensidad alta: autooscila pero queda acotado
        SpaceEcho e; e.prepare (SR);
        SpaceEchoParams ep; ep.timeMs = 120.0f; ep.heads = 0b111; ep.intensity = 1.1f; e.setParams (ep);
        std::vector<float> in ((size_t) SR * 10, 0.0f), out (in.size());
        for (int i = 0; i < 2000; ++i) in[(size_t) i] = 0.5f * std::sin (0.05f * i);
        e.process (in.data(), out.data(), (int) in.size());
        float pk = 0, late = 0;
        for (size_t i = 0; i < out.size(); ++i) { pk = std::max (pk, std::abs (out[i])); if (i > out.size() - (size_t) SR) late = std::max (late, std::abs (out[i])); }
        std::printf ("     echo al 110%%: pico %.3f, último segundo %.3f\n", pk, late);
        check (std::isfinite (pk) && pk <= 1.0f && late > 0.05f, "space echo autooscila sin explotar");

        // intensidad media en cada modo: las repeticiones se apagan solas (no se dispara al cambiar de modo)
        bool dies = true;
        for (int m = 0; m < kNumHeadModes; ++m)
        {
            SpaceEcho e2; e2.prepare (SR);
            SpaceEchoParams p2; p2.timeMs = 300.0f; p2.heads = headModeMask (m); p2.intensity = 0.7f; e2.setParams (p2);
            std::vector<float> i2 ((size_t) SR * 20, 0.0f), o2 (i2.size());
            for (int i = 0; i < 4000; ++i) i2[(size_t) i] = 0.5f * std::sin (0.03f * i);
            e2.process (i2.data(), o2.data(), (int) i2.size());
            float l2 = 0; for (size_t i = o2.size() - (size_t) SR; i < o2.size(); ++i) l2 = std::max (l2, std::abs (o2[i]));
            std::printf ("     echo modo %s al 70%%: último segundo %.4f\n", headModeLabel (m), l2);
            dies = dies && l2 < 0.02f;
        }
        check (dies, "space echo al 70 %: las repeticiones se apagan en todos los modos");
    }

    // 4d) Filtro master HP / LP de 24 dB
    {
        HpLpFilter off; off.prepare (SR); off.setParams (20.0f, 20000.0f, 0.3f);
        const float c = magnitudeDb ([&] (float* l, float* r, int n) { off.process (l, r, n); }, 3000.0f);
        HpLpFilter hp; hp.prepare (SR); hp.setParams (1000.0f, 20000.0f, 0.0f);
        float dummy = 0; (void) dummy;
        const float h1 = magnitudeDb ([&] (float* l, float* r, int n) { hp.process (l, r, n); }, 250.0f);
        HpLpFilter lp; lp.prepare (SR); lp.setParams (20.0f, 1000.0f, 0.0f);
        const float l1 = magnitudeDb ([&] (float* l, float* r, int n) { lp.process (l, r, n); }, 4000.0f);
        HpLpFilter rs; rs.prepare (SR); rs.setParams (20.0f, 1000.0f, 0.8f);
        const float pk = magnitudeDb ([&] (float* l, float* r, int n) { rs.process (l, r, n); }, 1000.0f, 0.1f);
        std::printf ("     master: off %.2f dB | HP1k a 250 Hz %.1f dB | LP1k a 4 kHz %.1f dB | resonancia en 1 kHz %+.1f dB\n", c, h1, l1, pk);
        check (std::abs (c) < 0.05f && h1 < -40.0f && l1 < -40.0f && pk > 6.0f, "HP / LP master 24 dB con resonancia");
    }

    // 4e) Sirena: suena, cae al soltar y se apaga
    {
        Siren s; s.prepare (SR);
        bool ok = true;
        for (int pr = 0; pr < kNumSirenPresets; ++pr)
        {
            s.reset(); s.noteOn (pr);
            std::vector<float> o ((size_t) SR, 0.0f);
            s.process (o.data(), (int) o.size(), 1.0f);
            float pk = 0, dc = 0; for (float v : o) { pk = std::max (pk, std::abs (v)); dc += v; }
            dc /= (float) o.size();
            s.noteOff();
            std::vector<float> t ((size_t) SR * 6, 0.0f);
            s.process (t.data(), (int) t.size(), 1.0f);
            std::printf ("     sirena %d: pico %.3f, DC %.4f, apagada %s\n", pr, pk, dc, s.isActive() ? "no" : "sí");
            ok = ok && pk > 0.1f && pk < 0.5f && std::abs (dc) < 0.01f && ! s.isActive();
        }
        check (ok, "sirenas: nivel, sin DC, se apagan");
    }

    // 5) Limitador: nunca pasa el techo
    {
        MasterDynamics md; md.prepare (SR); md.setDrive (0.3f); md.setCeilingDb (-0.3f); md.setLimiterOn (true);
        std::vector<float> L ((size_t) SR), R ((size_t) SR);
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 3.0f * std::sin (2.0f * kPi * 60.0f * (float) i / SR);
        for (size_t p = 0; p < L.size(); p += 512) md.process (L.data() + p, R.data() + p, (int) std::min<size_t> (512, L.size() - p));
        float pk = 0; for (float v : L) pk = std::max (pk, std::abs (v));
        std::printf ("     limitador: pico %.3f\n", pk);
        check (pk <= 1.0f, "limitador contiene picos de +10 dB");
    }

    // 6) Deck: reproducción, pitch, cue, hot cue, loop
    {
        auto track = std::make_shared<TrackAudio>();
        const int frames = (int) SR * 20;
        track->allocate (frames, 2, SR);
        for (int i = 0; i < frames; ++i)  // rampa: cada muestra codifica su posición
        {
            const auto v = (int16_t) ((i / 64) % 30000);
            track->left[(size_t) i] = v; track->right[(size_t) i] = v;
        }
        track->decodedFrames.store (frames);
        track->finished.store (true);

        Deck d; d.prepare (SR, 512);
        d.setTrack (track.get());
        std::vector<float> L (512), R (512);
        auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) d.process (L.data(), R.data(), 512); };

        d.pushCommand ({ DeckCmd::PlayToggle });
        run (86);    // ~1 s
        const double p1 = d.getPositionSeconds();
        std::printf ("     deck: tras 1 s de play, posición %.3f s\n", p1);
        check (std::abs (p1 - 86 * 512 / SR) < 0.03, "play arranca y avanza a velocidad normal");

        d.setPitch (0.5f, 0.16f);  // +8%
        const double a = d.getPositionSeconds(); run (86); const double b = d.getPositionSeconds();
        std::printf ("     deck: pitch +8%% -> avanza %.3f s en 1 s\n", b - a);
        check (std::abs ((b - a) / (86 * 512 / SR) - 1.08) < 0.01, "pitch cambia la velocidad");
        d.setPitch (0.0f, 0.16f);

        // hot cue 1 en 5 s y salto
        d.pushCommand ({ DeckCmd::Seek, 0, 5.0 }); run (1);
        d.pushCommand ({ DeckCmd::Hotcue, 0, 1.0 }); run (1);   // fija
        d.pushCommand ({ DeckCmd::Hotcue, 0, 0.0 }); run (1);
        d.pushCommand ({ DeckCmd::Seek, 0, 12.0 }); run (1);
        d.pushCommand ({ DeckCmd::Hotcue, 0, 1.0 }); run (1);   // salta
        d.pushCommand ({ DeckCmd::Hotcue, 0, 0.0 });
        std::printf ("     deck: hot cue -> %.3f s\n", d.getPositionSeconds());
        check (std::abs (d.getPositionSeconds() - 5.0) < 0.05, "hot cue se fija y salta");

        // loop de 1 s
        d.pushCommand ({ DeckCmd::Seek, 0, 8.0 }); run (1);
        d.pushCommand ({ DeckCmd::LoopIn, 0, 1.0 }); d.pushCommand ({ DeckCmd::LoopIn, 0, 0.0 }); run (86);
        d.pushCommand ({ DeckCmd::LoopOut, 0, 1.0 }); run (86 * 3);
        const double lp = d.getPositionSeconds();
        std::printf ("     deck: dentro del loop 8..9 s, posición %.3f s\n", lp);
        check (lp >= 7.99 && lp <= 9.02, "loop repite el tramo");
        d.pushCommand ({ DeckCmd::Reloop, 0, 1.0 }); run (200);
        check (d.getPositionSeconds() > 10.0, "reloop/exit sale del loop");

        // cue estilo CDJ: en pausa, CUE marca el punto; mantener reproduce; soltar vuelve
        d.pushCommand ({ DeckCmd::PlayToggle }); run (1);      // pausa
        const double here = d.getPositionSeconds();
        d.pushCommand ({ DeckCmd::Cue, 0, 1.0 }); run (40);
        const double held = d.getPositionSeconds();
        d.pushCommand ({ DeckCmd::Cue, 0, 0.0 }); run (1);
        std::printf ("     cue: %.3f -> mantenido %.3f -> soltado %.3f\n", here, held, d.getPositionSeconds());
        check (held > here + 0.3 && std::abs (d.getPositionSeconds() - here) < 0.02 && ! d.isPlaying(), "cue CDJ: preview y vuelta");

        // jog: scratch con el plato tocado, pausa = quieto
        d.pushCommand ({ DeckCmd::JogTouch, 0, 1.0 });
        const double s0 = d.getPositionSeconds();
        for (int k = 0; k < 43; ++k) { d.pushCommand ({ DeckCmd::JogScratch, 0, 4.6 }); run (1); }  // ~400 ticks/s
        d.pushCommand ({ DeckCmd::JogTouch, 0, 0.0 }); run (40);
        const double s1 = d.getPositionSeconds();
        std::printf ("     scratch: avanzó %.3f s en ~0,5 s girando a velocidad normal\n", s1 - s0);
        check (s1 - s0 > 0.3 && s1 - s0 < 0.7 && ! d.isPlaying(), "scratch sigue al plato");

        // CPU de reproducción
        d.pushCommand ({ DeckCmd::PlayToggle }); run (1);
        auto t0 = std::chrono::high_resolution_clock::now();
        run (86 * 10);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::printf ("     CPU deck: %.3f ms por segundo de audio\n", std::chrono::duration<double, std::milli> (t1 - t0).count() / 10.0);
    }

    // 7) CPU de la cadena master completa (aislador 5 + CFX + HP/LP + space echo + spring + dinámica)
    {
        Isolator iso; iso.prepare (SR); iso.configure (5, x5);
        DjFilter f; f.prepare (SR); f.setPosition (0.3f, 0.3f);
        SpringReverb sp; sp.prepare (SR);
        HpLpFilter hl; hl.prepare (SR); hl.setParams (80.0f, 9000.0f, 0.4f);
        SpaceEcho ec; ec.prepare (SR); SpaceEchoParams ep; ep.heads = 0b111; ec.setParams (ep);
        std::vector<float> E (512);
        MasterDynamics md; md.prepare (SR); md.setDrive (0.2f);
        std::vector<float> L (512, 0.1f), R (512, 0.1f), M (512, 0.1f);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < 86 * 10; ++b)
        {
            iso.process (L.data(), R.data(), 512); f.process (L.data(), R.data(), 512);
            ec.process (M.data(), E.data(), 512); hl.process (L.data(), R.data(), 512);
            sp.process (M.data(), L.data(), R.data(), 512, 0.3f); md.process (L.data(), R.data(), 512);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        std::printf ("     CPU master: %.3f ms por segundo de audio\n", std::chrono::duration<double, std::milli> (t1 - t0).count() / 10.0);
    }

    std::printf ("\n%s (%d fallas)\n", failures == 0 ? "TODO OK" : "HAY FALLAS", failures);
    return failures == 0 ? 0 : 1;
}
