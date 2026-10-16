// Engine.h — el motor de audio completo del preamp.
//
//  Deck 1 ─ trim ─ EQ aislador 3 bandas ─ filtro ─┬─ fader ─ crossfader ─┐
//  Deck 2 ─ trim ─ EQ aislador 3 bandas ─ filtro ─┤                      ├─ bus master
//                                                 └─ CUE ─ auriculares   │
//  envío (canal 1 / 2 / ambos, post-fader) ─┬─ TAPE ECHO ────────────────┤
//                                           └─ SPRING ───────────────────┤
//  sirena, samples, entrada en vivo ─────────────────────────────────────┘
//  bus master ─ sub boost ─ AISLADOR 3/4/5 ─ filtro master ─ nivel ─ drive/limitador ─ salida 1-2
//  auriculares (cue / master) ─ salida 3-4 (FLX4)
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

#include "Params.h"
#include "Dsp.h"
#include "Deck.h"
#include "Loader.h"
#include "Sampler.h"
#include "TapeEchoDSP.h"
#include "SirenDSP.h"

namespace ct
{

constexpr int kNumEchoDivisions = 7;
inline float echoDivisionBeats (int i)
{
    static const float d[kNumEchoDivisions] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.0f / 3.0f };
    return d[juce::jlimit (0, kNumEchoDivisions - 1, i)];
}
inline const char* echoDivisionLabel (int i)
{
    static const char* l[kNumEchoDivisions] = { "1/16", "1/8", "1/8·", "1/4", "1/4·", "1/2", "1/4T" };
    return l[juce::jlimit (0, kNumEchoDivisions - 1, i)];
}

// nombres de las bandas del aislador según las vías
inline const char* isoBandName (int mode, int band)
{
    static const char* n3[] = { "GRAVES", "MEDIOS", "AGUDOS" };
    static const char* n4[] = { "SUB", "BASS", "MID", "TOP" };
    static const char* n5[] = { "SUB", "BASS", "LO-MID", "HI-MID", "TOP" };
    if (mode == 0) return band < 3 ? n3[band] : "";
    if (mode == 1) return band < 4 ? n4[band] : "";
    return band < 5 ? n5[band] : "";
}
inline int isoBands (int mode) { return mode + 3; }

class Engine : public juce::AudioIODeviceCallback
{
public:
    Engine();
    ~Engine() override;

    Params params;

    // --- decks
    Deck& deck (int i) { return decks[(size_t) i]; }
    bool loadTrack (int deckIndex, const juce::File& file, juce::String& error);
    std::shared_ptr<TrackAudio> getTrack (int deckIndex) const { return deckTracks[(size_t) deckIndex]; }
    TrackLoader& getLoader() { return loader; }
    void purgeOldTracks();

    // --- samples
    Sampler& getSampler() { return sampler; }
    bool loadSample (int slot, const juce::File& file);
    juce::String getSampleName (int slot) const;
    juce::File getSampleFile (int slot) const { return sampleFiles[(size_t) slot]; }

    // --- efectos: "mantener" desde varias fuentes
    Hold echoThrow, revThrow, echoFreeze, echoShorter, echoLonger;
    std::array<Hold, kNumSirenPresets> sirenHold;
    void setSiren (int preset, int src, bool down);
    int  getSirenPlaying() const { return sirenPlaying.load(); }
    bool isSirenHeld() const { for (auto& h : sirenHold) if (h.held()) return true; return false; }
    void killEcho() { echoKillReq.fetch_add (1); }
    bool isEchoSending() const { return echoSending.load(); }
    bool isSpringSending() const { return revSending.load(); }
    void releaseAllHolds()
    {
        for (auto* h : { &echoThrow, &revThrow, &echoFreeze, &echoShorter, &echoLonger }) h->mask.store (0);
        for (auto& h : sirenHold) h.mask.store (0);
    }

    // --- tempo del echo
    void tap();
    void selectEchoDivision (int i);
    void stepEchoDivision (int delta) { selectEchoDivision ((echoDivision.load() + delta + kNumEchoDivisions) % kNumEchoDivisions); }
    int   getEchoDivision() const  { return echoDivision.load(); }
    float getBpm() const           { return bpm.load(); }
    bool  echoTimeIsSynced() const;
    float getEchoGlideMs() const   { return echoGlideMs.load(); }

    // --- salidas
    void setOutputPairs (int masterPair, int phonesPair) { masterOutPair.store (masterPair); phonesOutPair.store (phonesPair); }
    int  getMasterPair() const { return masterOutPair.load(); }
    int  getPhonesPair() const { return phonesOutPair.load(); }
    int  getNumOutputs() const { return numOutputs.load(); }

    // --- medidores (0..1 lineal, con caída)
    float getChannelLevel (int i) const { return chanMeter[(size_t) i].load(); }
    float getMasterLevel (int ch) const { return masterMeter[(size_t) ch].load(); }
    float getLimiterReductionDb() const { return grDb.load(); }

    // --- grabación del master
    bool startRecording (juce::File& fileOut);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    double getRecordingSeconds() const { return recordedSamples.load() / juce::jmax (1.0, currentSr.load()); }

    // AudioIODeviceCallback
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;

private:
    void processChunk (const float* const* in, int numIn, float* const* out, int numOut, int offset, int n);

    static constexpr int kMaxBlock = 1024;

    std::array<Deck, 2> decks;
    std::array<std::shared_ptr<TrackAudio>, 2> deckTracks;
    struct Retired { std::shared_ptr<TrackAudio> track; double when; };
    std::vector<Retired> graveyard;
    TrackLoader loader;

    Sampler sampler;
    std::array<std::shared_ptr<SampleData>, Sampler::kSlots> sampleData;
    std::array<juce::File, Sampler::kSlots> sampleFiles;
    std::vector<std::shared_ptr<SampleData>> sampleGraveyard;

    // DSP
    std::array<Isolator, 2> chanEq;
    std::array<DjFilter, 2> chanFilter;
    std::array<Ramp, 2> trimRamp, faderRamp;
    Isolator masterIso;
    SubBoost subBoost;
    DjFilter masterFilter;
    MasterDynamics dynamics;
    Ramp masterLevelRamp, modeFade, echoSendRamp, revSendRamp, echoRetRamp, revToRamp, sirenRamp, sampRamp, inRamp, headRamp;
    TapeEcho echo;
    SpringReverb spring;
    Siren siren;
    int isoModeActive = 1, isoModePending = -1;

    std::atomic<int> sirenLast { 0 }, sirenPlaying { -1 };
    std::atomic<int> echoKillReq { 0 };
    std::atomic<bool> echoSending { false }, revSending { false };
    int echoKillSeen = 0;

    juce::SpinLock tapLock;
    double lastTapMs = 0.0;
    juce::Array<double> tapIntervals;
    std::atomic<float> bpm { 75.0f };
    std::atomic<int> echoDivision { 2 };
    std::atomic<float> echoGlideMs { 0.0f };

    std::atomic<int> masterOutPair { 0 }, phonesOutPair { 1 }, numOutputs { 2 };
    std::array<std::atomic<float>, 2> chanMeter {}, masterMeter {};
    std::atomic<float> grDb { 0.0f };
    std::atomic<double> currentSr { 48000.0 };

    // buffers de trabajo
    std::vector<float> aL, aR, bL, bR, mixL, mixR, cueL, cueR, echoIn, echoOut, revIn, sirenBuf, sampL, sampR, tmp;

    // grabación
    juce::TimeSliceThread writerThread { "Control Tower: grabación" };
    juce::SpinLock writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<bool> recording { false };
    std::atomic<double> recordedSamples { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace ct
