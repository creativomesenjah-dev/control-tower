// Sampler.h — 16 ranuras de one-shots (dub plates, efectos, voces). Sin JUCE.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace ct
{

struct SampleData
{
    std::vector<float> left, right;
    float sampleRate = 44100.0f;
    std::string name;
};

class Sampler
{
public:
    static constexpr int kSlots = 16;

    void prepare (float deviceSampleRate) { deviceSr = deviceSampleRate; }

    // hilo de mensajes
    void setSlot (int i, const SampleData* d) { slots[(size_t) i].pending.store (d); slots[(size_t) i].changed.store (true); }
    void trigger (int i)    { slots[(size_t) i].trig.fetch_add (1); }
    void stop (int i)       { slots[(size_t) i].stopReq.fetch_add (1); }
    void stopAll()          { for (int i = 0; i < kSlots; ++i) stop (i); }
    bool isPlaying (int i) const { return slots[(size_t) i].playingState.load(); }
    const SampleData* get (int i) const { return slots[(size_t) i].pending.load(); }

    // hilo de audio: suma en L/R (estéreo)
    void process (float* L, float* R, int n)
    {
        for (auto& s : slots)
        {
            if (s.changed.exchange (false)) { s.data = s.pending.load(); s.active = false; }
            const int t = s.trig.load();
            if (t != s.trigSeen) { s.trigSeen = t; if (s.data != nullptr) { s.pos = 0.0; s.active = true; } }
            const int st = s.stopReq.load();
            if (st != s.stopSeen) { s.stopSeen = st; s.active = false; }

            if (s.active && s.data != nullptr)
            {
                const auto& d = *s.data;
                const double step = d.sampleRate / deviceSr;
                const auto len = (double) d.left.size();
                for (int i = 0; i < n; ++i)
                {
                    const auto idx = (size_t) s.pos;
                    if ((double) idx + 1.0 >= len) { s.active = false; break; }
                    const float fr = (float) (s.pos - (double) idx);
                    L[i] += d.left[idx]  + fr * (d.left[idx + 1]  - d.left[idx]);
                    R[i] += d.right[idx] + fr * (d.right[idx + 1] - d.right[idx]);
                    s.pos += step;
                }
            }
            s.playingState.store (s.active);
        }
    }

private:
    struct Slot
    {
        std::atomic<const SampleData*> pending { nullptr };
        std::atomic<bool> changed { false }, playingState { false };
        std::atomic<int> trig { 0 }, stopReq { 0 };
        const SampleData* data = nullptr;
        int trigSeen = 0, stopSeen = 0;
        double pos = 0.0;
        bool active = false;
    };
    std::array<Slot, kSlots> slots;
    float deviceSr = 48000.0f;
};

} // namespace ct
