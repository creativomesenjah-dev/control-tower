#include "Engine.h"

namespace ct
{

Engine::Engine()
{
    for (auto* v : { &aL, &aR, &bL, &bR, &mixL, &mixR, &cueL, &cueR, &echoIn, &echoOut, &revIn, &sirenBuf, &sampL, &sampR, &tmp })
        v->assign (kMaxBlock, 0.0f);
    writerThread.startThread();
}

Engine::~Engine()
{
    stopRecording();
    writerThread.stopThread (2000);
}

// ---------------------------------------------------------------------------
bool Engine::loadTrack (int i, const juce::File& file, juce::String& error)
{
    if (decks[(size_t) i].isPlaying())
    {
        error = "El deck " + juce::String (i + 1) + u8 (" está sonando: pausalo para cargar otro tema.");
        return false;
    }
    auto track = loader.load (file);
    if (track->failed.load())
    {
        error = u8 ("No se pudo abrir: ") + file.getFileName();
        return false;
    }
    if (auto old = deckTracks[(size_t) i])
    {
        old->cancel.store (true);
        graveyard.push_back ({ old, juce::Time::getMillisecondCounterHiRes() });
    }
    deckTracks[(size_t) i] = track;
    decks[(size_t) i].setTrack (track.get());
    return true;
}

void Engine::purgeOldTracks()
{
    // el hilo de audio deja de usar un tema a más tardar en el bloque siguiente; 3 s es más que de sobra
    const double now = juce::Time::getMillisecondCounterHiRes();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const Retired& r) { return now - r.when > 3000.0; }),
                     graveyard.end());
    if (sampleGraveyard.size() > 32)
        sampleGraveyard.erase (sampleGraveyard.begin(), sampleGraveyard.begin() + 16);
}

bool Engine::loadSample (int slot, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (loader.getFormats().createReaderFor (file));
    if (reader == nullptr) return false;
    const auto len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    if (len <= 0) return false;

    juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    auto d = std::make_shared<SampleData>();
    d->sampleRate = (float) reader->sampleRate;
    d->name = file.getFileNameWithoutExtension().toStdString();
    d->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    const float* r = reader->numChannels > 1 ? buf.getReadPointer (1) : buf.getReadPointer (0);
    d->right.assign (r, r + len);

    if (sampleData[(size_t) slot] != nullptr) sampleGraveyard.push_back (sampleData[(size_t) slot]);
    sampleData[(size_t) slot] = d;
    sampleFiles[(size_t) slot] = file;
    sampler.setSlot (slot, d.get());
    return true;
}

juce::String Engine::getSampleName (int slot) const
{
    const auto& d = sampleData[(size_t) slot];
    return d != nullptr ? juce::String (juce::CharPointer_UTF8 (d->name.c_str())) : juce::String();
}

// ---------------------------------------------------------------------------
void Engine::setSiren (int preset, int src, bool down)
{
    preset = juce::jlimit (0, kNumSirenPresets - 1, preset);
    sirenHold[(size_t) preset].set (src, down);
    if (down) sirenLast.store (preset);
}

void Engine::tap()
{
    const juce::SpinLock::ScopedLockType sl (tapLock);
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double interval = now - lastTapMs;
    lastTapMs = now;
    if (interval > 250.0 && interval < 1500.0)
    {
        tapIntervals.add (interval);
        if (tapIntervals.size() > 4) tapIntervals.remove (0);
        double avg = 0.0;
        for (auto v : tapIntervals) avg += v;
        avg /= tapIntervals.size();
        bpm.store ((float) juce::jlimit (40.0, 240.0, 60000.0 / avg));
        selectEchoDivision (echoDivision.load());
    }
    else
    {
        tapIntervals.clearQuick();
    }
}

void Engine::selectEchoDivision (int i)
{
    i = juce::jlimit (0, kNumEchoDivisions - 1, i);
    echoDivision.store (i);
    params.set (P::EchoTime, juce::jlimit (20.0f, 2000.0f, 60000.0f / bpm.load() * echoDivisionBeats (i)));
}

bool Engine::echoTimeIsSynced() const
{
    const float expected = juce::jlimit (20.0f, 2000.0f, 60000.0f / bpm.load() * echoDivisionBeats (echoDivision.load()));
    return std::abs (params.get (P::EchoTime) - expected) < 1.5f;
}

// ---------------------------------------------------------------------------
bool Engine::startRecording (juce::File& fileOut)
{
    stopRecording();
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Control Tower");
    dir.createDirectory();
    fileOut = dir.getChildFile ("Sesion " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");

    std::unique_ptr<juce::OutputStream> stream (fileOut.createOutputStream());
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (currentSr.load())
                                                                          .withNumChannels (2)
                                                                          .withBitsPerSample (16));
    if (w == nullptr) return false;

    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 65536);
    recordedSamples.store (0.0);
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        writer = std::move (threaded);
    }
    recording.store (true);
    return true;
}

void Engine::stopRecording()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType sl (writerLock);
        old = std::move (writer);
    }
    recording.store (false);
    old.reset();   // vacía el buffer y cierra el archivo (fuera del hilo de audio)
}

// ---------------------------------------------------------------------------
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sr = (float) device->getCurrentSampleRate();
    currentSr.store (sr);
    numOutputs.store (device->getActiveOutputChannels().countNumberOfSetBits());

    for (auto& d : decks) d.prepare (sr, kMaxBlock);
    sampler.prepare (sr);

    const float eqX[] = { 300.0f, 3000.0f };
    for (int i = 0; i < 2; ++i)
    {
        chanEq[(size_t) i].prepare (sr);
        chanEq[(size_t) i].configure (3, eqX);
        chanFilter[(size_t) i].prepare (sr);
    }
    isoModeActive = params.choice (P::IsoMode);
    const float x3[] = { 300.0f, 3000.0f }, x4[] = { 100.0f, 400.0f, 2500.0f }, x5[] = { 80.0f, 250.0f, 1000.0f, 4000.0f };
    const float* xs[] = { x3, x4, x5 };
    masterIso.prepare (sr);
    masterIso.configure (isoBands (isoModeActive), xs[isoModeActive]);
    subBoost.prepare (sr);
    masterFilter.prepare (sr);
    dynamics.prepare (sr);
    dynamics.setCeilingDb (-0.3f);
    echo.prepare (sr);
    spring.prepare (sr);
    siren.prepare (sr);
    modeFade.snap (1.0f);
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

    for (int offset = 0; offset < numSamples; offset += kMaxBlock)
        processChunk (in, numIn, out, numOut, offset, juce::jmin (kMaxBlock, numSamples - offset));
}

static inline float peakOf (const float* a, const float* b, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; ++i) m = juce::jmax (m, std::abs (a[i]), std::abs (b[i]));
    return m;
}

static inline void meter (std::atomic<float>& m, float peak, int n, float sr)
{
    const float decay = std::exp (-(float) n / (0.25f * sr));   // caída de ~250 ms
    m.store (juce::jmax (peak, m.load() * decay));
}

void Engine::processChunk (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n)
{
    auto& p = params;
    const float sr = (float) currentSr.load();

    // ---------------- decks y canales
    static const float ranges[] = { 0.08f, 0.16f, 0.5f };
    decks[0].setPitch (p.get (P::PitchA), ranges[p.choice (P::RangeA)]);
    decks[1].setPitch (p.get (P::PitchB), ranges[p.choice (P::RangeB)]);
    decks[0].process (aL.data(), aR.data(), n);
    decks[1].process (bL.data(), bR.data(), n);

    juce::FloatVectorOperations::clear (mixL.data(), n);
    juce::FloatVectorOperations::clear (mixR.data(), n);
    juce::FloatVectorOperations::clear (cueL.data(), n);
    juce::FloatVectorOperations::clear (cueR.data(), n);
    juce::FloatVectorOperations::clear (echoIn.data(), n);
    juce::FloatVectorOperations::clear (revIn.data(), n);

    const float xf = p.get (P::Crossfader);
    const float xfGain[2] = { juce::jmin (1.0f, 2.0f * (1.0f - xf)), juce::jmin (1.0f, 2.0f * xf) };
    const int src = p.choice (P::EchoSrc);   // 0 canal 1, 1 canal 2, 2 ambos

    const P trimP[2] = { P::TrimA, P::TrimB }, hiP[2] = { P::HiA, P::HiB }, midP[2] = { P::MidA, P::MidB },
            lowP[2] = { P::LowA, P::LowB }, filP[2] = { P::FilterA, P::FilterB }, fadP[2] = { P::FaderA, P::FaderB },
            cueP[2] = { P::CueA, P::CueB };
    float* chL[2] = { aL.data(), bL.data() };
    float* chR[2] = { aR.data(), bR.data() };

    for (int c = 0; c < 2; ++c)
    {
        // trim
        trimRamp[(size_t) c].set (dbToGain (p.get (trimP[c])));
        {
            Ramp r = trimRamp[(size_t) c];
            r.apply (chL[c], n);
            trimRamp[(size_t) c].apply (chR[c], n);
        }
        // EQ aislador (graves, medios, agudos)
        auto& eq = chanEq[(size_t) c];
        eq.setBandGain (0, Params::eqGain (p.get (lowP[c])));
        eq.setBandGain (1, Params::eqGain (p.get (midP[c])));
        eq.setBandGain (2, Params::eqGain (p.get (hiP[c])));
        eq.process (chL[c], chR[c], n);
        // filtro de canal
        chanFilter[(size_t) c].setPosition (p.get (filP[c]), 0.35f);
        chanFilter[(size_t) c].process (chL[c], chR[c], n);

        // nivel pre-fader para medidores y cue
        meter (chanMeter[(size_t) c], peakOf (chL[c], chR[c], n), n, sr);
        if (p.on (cueP[c]))
        {
            juce::FloatVectorOperations::add (cueL.data(), chL[c], n);
            juce::FloatVectorOperations::add (cueR.data(), chR[c], n);
        }

        // fader + crossfader (curva suave en el fader)
        const float f = p.get (fadP[c]);
        faderRamp[(size_t) c].set (f * f * xfGain[c]);
        {
            Ramp r = faderRamp[(size_t) c];
            r.apply (chL[c], n);
            faderRamp[(size_t) c].apply (chR[c], n);
        }
        juce::FloatVectorOperations::add (mixL.data(), chL[c], n);
        juce::FloatVectorOperations::add (mixR.data(), chR[c], n);

        // envíos (post-fader, mono): la ganancia del envío se aplica después
        if (src == 2 || src == c)
            for (int i = 0; i < n; ++i)
                echoIn[(size_t) i] += 0.5f * (chL[c][i] + chR[c][i]);
    }

    // revIn usa la misma fuente que el echo
    juce::FloatVectorOperations::copy (revIn.data(), echoIn.data(), n);

    // ganancias de envío
    const float eSend = juce::jlimit (0.0f, 1.0f, p.get (P::EchoSend) + (p.on (P::EchoLatch) ? 1.0f : 0.0f) + (echoThrow.held() ? 1.0f : 0.0f));
    const float rSend = juce::jlimit (0.0f, 1.0f, p.get (P::RevSend) + (p.on (P::RevLatch) ? 1.0f : 0.0f) + (revThrow.held() ? 1.0f : 0.0f));
    echoSendRamp.set (eSend);
    revSendRamp.set (rSend);
    echoSendRamp.apply (echoIn.data(), n);
    revSendRamp.apply (revIn.data(), n);

    // ---------------- sirena
    {
        int held = -1;
        const int last = sirenLast.load();
        if (sirenHold[(size_t) last].held()) held = last;
        else for (int i = 0; i < kNumSirenPresets; ++i) if (sirenHold[(size_t) i].held()) { held = i; break; }

        if (held >= 0 && (! siren.isHeld() || siren.getPreset() != held)) siren.noteOn (held);
        else if (held < 0 && siren.isHeld()) siren.noteOff();
        siren.setXY (p.get (P::SirenPitch), p.get (P::SirenRate));
        sirenPlaying.store (siren.isActive() ? siren.getPreset() : -1);

        juce::FloatVectorOperations::clear (sirenBuf.data(), n);
        if (siren.process (sirenBuf.data(), n, 1.0f))
        {
            sirenRamp.set (dbToGain (p.get (P::SirenLevel)));
            sirenRamp.apply (sirenBuf.data(), n);
            juce::FloatVectorOperations::add (mixL.data(), sirenBuf.data(), n);
            juce::FloatVectorOperations::add (mixR.data(), sirenBuf.data(), n);
            juce::FloatVectorOperations::addWithMultiply (echoIn.data(), sirenBuf.data(), p.get (P::SirenEcho), n);
        }
    }

    // ---------------- samples
    {
        juce::FloatVectorOperations::clear (sampL.data(), n);
        juce::FloatVectorOperations::clear (sampR.data(), n);
        sampler.process (sampL.data(), sampR.data(), n);
        const float g = p.get (P::SampLevel) <= -39.9f ? 0.0f : dbToGain (p.get (P::SampLevel));
        sampRamp.set (g);
        { Ramp r = sampRamp; r.apply (sampL.data(), n); sampRamp.apply (sampR.data(), n); }
        juce::FloatVectorOperations::add (mixL.data(), sampL.data(), n);
        juce::FloatVectorOperations::add (mixR.data(), sampR.data(), n);
        const float se = 0.5f * p.get (P::SampEcho);
        juce::FloatVectorOperations::addWithMultiply (echoIn.data(), sampL.data(), se, n);
        juce::FloatVectorOperations::addWithMultiply (echoIn.data(), sampR.data(), se, n);
    }

    // ---------------- entrada en vivo (micrófono / línea)
    if (numIn > 0 && in[0] != nullptr)
    {
        const float g = p.on (P::InOn) ? dbToGain (p.get (P::InGain)) : 0.0f;
        inRamp.set (g);
        const float* il = in[0] + offset;
        const float* ir = (numIn > 1 && in[1] != nullptr) ? in[1] + offset : il;
        if (g > 0.0f || inRamp.current > 0.0f)
        {
            juce::FloatVectorOperations::copy (sampL.data(), il, n);
            juce::FloatVectorOperations::copy (sampR.data(), ir, n);
            { Ramp r = inRamp; r.apply (sampL.data(), n); inRamp.apply (sampR.data(), n); }
            juce::FloatVectorOperations::add (mixL.data(), sampL.data(), n);
            juce::FloatVectorOperations::add (mixR.data(), sampR.data(), n);
            const float ie = 0.5f * p.get (P::InEcho);
            juce::FloatVectorOperations::addWithMultiply (echoIn.data(), sampL.data(), ie, n);
            juce::FloatVectorOperations::addWithMultiply (echoIn.data(), sampR.data(), ie, n);
        }
    }

    // ---------------- tape echo
    {
        const int k = echoKillReq.load();
        if (k != echoKillSeen) { echoKillSeen = k; echo.triggerKill(); }

        TapeEchoParams ep;
        ep.timeMs     = p.get (P::EchoTime);
        ep.feedback   = p.get (P::EchoFb) * 0.01f;
        ep.inertiaSec = p.get (P::EchoIner);
        ep.wowFlutter = p.get (P::EchoWow) * 0.01f;
        ep.drive      = p.get (P::EchoSat) * 0.01f;
        ep.lowCutHz   = p.get (P::EchoLow);
        ep.highCutHz  = p.get (P::EchoHigh);
        ep.inputGain  = 1.0f;
        ep.outputGain = 1.0f;
        ep.sendOpen   = true;
        ep.throwHeld  = false;
        ep.freeze     = echoFreeze.held();
        echo.setParams (ep);
        echo.process (echoIn.data(), nullptr, echoOut.data(), nullptr, n);
        echoGlideMs.store (echo.getCurrentDelayMs());

        const float ret = p.get (P::EchoRet) <= -39.9f ? 0.0f : dbToGain (p.get (P::EchoRet));
        echoRetRamp.set (ret);
        echoRetRamp.apply (echoOut.data(), n);
        juce::FloatVectorOperations::add (mixL.data(), echoOut.data(), n);
        juce::FloatVectorOperations::add (mixR.data(), echoOut.data(), n);
        // el echo también alimenta al spring (clásico de dub)
        revToRamp.set (p.get (P::EchoToRev));
        revToRamp.applyAdd (echoOut.data(), revIn.data(), n);
    }

    // ---------------- spring
    {
        spring.setParams (p.get (P::RevSize), p.get (P::RevDecay), p.get (P::RevTone));
        const float ret = p.get (P::RevRet) <= -39.9f ? 0.0f : dbToGain (p.get (P::RevRet));
        spring.process (revIn.data(), mixL.data(), mixR.data(), n, ret);
    }

    // ---------------- master
    {
        // cambio de vías del aislador: bajar, reconfigurar, subir (sin clicks)
        const int mode = p.choice (P::IsoMode);
        if (mode != isoModeActive && isoModePending < 0) { isoModePending = mode; modeFade.set (0.0f); }
        else if (isoModePending >= 0 && modeFade.current == 0.0f)
        {
            const float x3[] = { 300.0f, 3000.0f }, x4[] = { 100.0f, 400.0f, 2500.0f }, x5[] = { 80.0f, 250.0f, 1000.0f, 4000.0f };
            const float* xs[] = { x3, x4, x5 };
            isoModeActive = isoModePending;
            isoModePending = -1;
            masterIso.configure (isoBands (isoModeActive), xs[isoModeActive]);
            modeFade.set (1.0f);
        }

        subBoost.setOn (p.on (P::SubBoost));
        subBoost.process (mixL.data(), mixR.data(), n);

        const P bandP[5] = { P::Iso1, P::Iso2, P::Iso3, P::Iso4, P::Iso5 };
        const P killP[5] = { P::Kill1, P::Kill2, P::Kill3, P::Kill4, P::Kill5 };
        for (int b = 0; b < isoBands (isoModeActive); ++b)
        {
            const float db = p.get (bandP[b]);
            masterIso.setBandGain (b, (p.on (killP[b]) || db <= -39.9f) ? 0.0f : dbToGain (db));
        }
        masterIso.process (mixL.data(), mixR.data(), n);

        masterFilter.setPosition (p.get (P::MFilter), p.get (P::MRes));
        masterFilter.process (mixL.data(), mixR.data(), n);

        const float lvl = (p.get (P::MLevel) <= -39.9f ? 0.0f : dbToGain (p.get (P::MLevel)));
        masterLevelRamp.set (lvl);
        { Ramp r = masterLevelRamp; r.apply (mixL.data(), n); masterLevelRamp.apply (mixR.data(), n); }
        { Ramp r = modeFade; r.apply (mixL.data(), n); modeFade.apply (mixR.data(), n); }

        dynamics.setDrive (p.get (P::Drive));
        dynamics.setLimiterOn (p.on (P::Limiter));
        dynamics.process (mixL.data(), mixR.data(), n);
        grDb.store (dynamics.getGainReductionDb());

        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < n; ++i) { pl = juce::jmax (pl, std::abs (mixL[(size_t) i])); pr = juce::jmax (pr, std::abs (mixR[(size_t) i])); }
        meter (masterMeter[0], pl, n, sr);
        meter (masterMeter[1], pr, n, sr);
    }

    // ---------------- salidas
    const int mp = masterOutPair.load(), hp = phonesOutPair.load();
    auto writePair = [&] (int pair, const float* l, const float* r)
    {
        const int c0 = pair * 2, c1 = pair * 2 + 1;
        if (c0 < numOut && out[c0] != nullptr) juce::FloatVectorOperations::copy (out[c0] + offset, l, n);
        if (c1 < numOut && out[c1] != nullptr) juce::FloatVectorOperations::copy (out[c1] + offset, r, n);
    };
    writePair (mp, mixL.data(), mixR.data());

    if (hp >= 0 && hp != mp && hp * 2 < numOut)
    {
        const float hm = p.get (P::HeadMix);
        for (int i = 0; i < n; ++i)
        {
            cueL[(size_t) i] = (1.0f - hm) * cueL[(size_t) i] + hm * mixL[(size_t) i];
            cueR[(size_t) i] = (1.0f - hm) * cueR[(size_t) i] + hm * mixR[(size_t) i];
        }
        headRamp.set (p.get (P::HeadLevel) <= -39.9f ? 0.0f : dbToGain (p.get (P::HeadLevel)));
        { Ramp r = headRamp; r.apply (cueL.data(), n); headRamp.apply (cueR.data(), n); }
        juce::FloatVectorOperations::clip (cueL.data(), cueL.data(), -1.0f, 1.0f, n);
        juce::FloatVectorOperations::clip (cueR.data(), cueR.data(), -1.0f, 1.0f, n);
        writePair (hp, cueL.data(), cueR.data());
    }

    // ---------------- grabación
    if (recording.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (writerLock);
        if (sl.isLocked() && writer != nullptr)
        {
            const float* chans[2] = { mixL.data(), mixR.data() };
            if (writer->write (chans, n))
                recordedSamples.store (recordedSamples.load() + n);
        }
    }
}

} // namespace ct
