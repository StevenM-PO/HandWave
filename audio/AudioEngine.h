#pragma once

#include "DrawnFilter.h"
#include "Envelope.h"
#include "ParamSmoother.h"
#include "SpscQueue.h"
#include "Wavetable.h"
#include "WavetableOscillator.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>

struct ma_context; // from miniaudio.h, kept out of this header
struct ma_device;

namespace hw {

// Owns the audio output device and the synth voice:
//   oscillator -> drawn filter -> amp envelope -> volume -> soft clip -> output
// with the filter envelope (times its amount) added to the filter cutoff.
// A note sounds while the gate is on (setGate).
//
// Threading model:
//   - The "control" thread (the UI thread) calls start/stop and the setters.
//   - miniaudio's real-time thread calls render(). It must never block or
//     allocate, so all communication is via atomics and lock-free queues.
//
// Hand-off of large objects (wavetables, filter curves):
//   UI thread builds one --> atomic mailbox --> audio thread
//   audio thread finishes with the old one --> retired queue --> UI thread
//   frees it in collectGarbage(). Memory is only ever freed off the audio thread.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Open and start the default output device. Returns false on failure,
    // with a description in lastError().
    bool start();
    void stop();
    bool isRunning() const { return running_; }
    const std::string& lastError() const { return lastError_; }
    double sampleRate() const { return sampleRate_; }

    // ---- Control thread API ----
    void setWavetable(std::unique_ptr<Wavetable> table);
    void setFrequency(float hz) { targetFrequency_.store(hz, std::memory_order_relaxed); }
    void setVolume(float volume) { volume_.store(volume, std::memory_order_relaxed); }
    void setGate(bool on) { gate_.store(on, std::memory_order_relaxed); }

    void setFilterCurve(std::unique_ptr<FilterCurve> curve);
    void setFilterEnabled(bool on) { filterEnabled_.store(on, std::memory_order_relaxed); }
    void setFilterCutoff(float shiftOctaves) { filterShift_.store(shiftOctaves, std::memory_order_relaxed); }
    void setFilterLoop(bool on) { filterLoop_.store(on, std::memory_order_relaxed); }
    void setResonance(float curveX, float levelDb)
    {
        resonanceX_.store(curveX, std::memory_order_relaxed);
        resonanceDb_.store(levelDb, std::memory_order_relaxed);
    }

    // Envelopes: kAmpEnvelope shapes loudness, kFilterEnvelope moves the cutoff.
    static constexpr int kAmpEnvelope = 0;
    static constexpr int kFilterEnvelope = 1;
    void setEnvelopeMode(int envelope, EnvelopeMode mode);
    void setAdsr(int envelope, const AdsrParams& params);
    void setDrawnTiming(int envelope, const DrawnTiming& timing);
    void setEnvelopeCurve(int envelope, std::unique_ptr<EnvelopeCurve> curve);
    // How far (octaves) the filter envelope moves the cutoff at full level.
    void setFilterEnvAmount(float octaves) { filterEnvAmount_.store(octaves, std::memory_order_relaxed); }

    // Where an envelope is right now, for on-screen playheads.
    struct EnvelopeStatus {
        EnvelopeStage stage = EnvelopeStage::Idle;
        double stageSeconds = 0.0;
        float level = 0.0f;
    };
    EnvelopeStatus envelopeStatus(int envelope) const;

    // Free tables and curves the audio thread has finished with. Call regularly.
    void collectGarbage();

    // ---- Audio thread only ----
    // Fill `frames` interleaved frames of `channels` channels.
    void render(float* output, unsigned frames, unsigned channels);

private:
    // Control-rate block: filter settings are updated once per block.
    static constexpr int kBlock = 256;

    void prepare(double sampleRate);
    void exchangeTables();
    void exchangeFilterCurve();
    void renderBlock(float* output, int frames, unsigned channels);
    void updateEnvelopes();

    // One envelope plus the settings the UI sends it.
    struct EnvelopeSlot {
        Envelope envelope;
        std::atomic<int> mode{0};
        std::atomic<float> attack{0.005f}, decay{0.3f}, sustain{0.8f}, release{0.3f};
        std::atomic<float> split1{1.0f / 3.0f}, split2{2.0f / 3.0f}, timespan{1.0f}, drawnSustain{0.6f};
        std::atomic<EnvelopeCurve*> pendingCurve{nullptr};
        const EnvelopeCurve* curve = nullptr; // audio thread's current drawing
        EnvelopeCurve* heldCurve = nullptr;   // taken from the mailbox, waiting to swap in
        // Published by the audio thread for the UI.
        std::atomic<int> statusStage{0};
        std::atomic<float> statusSeconds{0.0f}, statusLevel{0.0f};
    };

    std::unique_ptr<ma_context> context_;
    std::unique_ptr<ma_device> device_;
    bool running_ = false;
    std::string lastError_;
    double sampleRate_ = 48000.0;

    // Owned by the audio thread while running.
    WavetableOscillator oscillator_;
    ParamSmoother frequencySmoother_;
    ParamSmoother gainSmoother_;
    std::unique_ptr<DrawnFilter> filter_;
    const FilterCurve* filterCurve_ = nullptr;
    FilterCurve* heldCurve_ = nullptr; // taken from the mailbox, waiting to swap in
    ParamSmoother shiftSmoother_;     // advanced once per block
    ParamSmoother resonanceXSmoother_;
    ParamSmoother resonanceDbSmoother_;
    ParamSmoother filterMixSmoother_; // 0 = bypassed, 1 = filtered; per sample
    std::array<float, kBlock> dry_{};
    std::array<float, kBlock> wet_{};
    std::array<EnvelopeSlot, 2> envelopes_;
    bool lastGate_ = false;

    std::atomic<Wavetable*> pending_{nullptr};
    SpscQueue<const Wavetable*, 64> retired_;
    std::atomic<FilterCurve*> pendingCurve_{nullptr};
    SpscQueue<const FilterCurve*, 64> retiredCurves_;
    SpscQueue<const EnvelopeCurve*, 64> retiredEnvelopeCurves_;

    std::atomic<float> targetFrequency_{110.0f};
    std::atomic<float> volume_{0.5f};
    std::atomic<bool> gate_{false};
    std::atomic<bool> filterEnabled_{true};
    std::atomic<float> filterShift_{0.0f};
    std::atomic<bool> filterLoop_{false};
    std::atomic<float> resonanceX_{0.5f};
    std::atomic<float> resonanceDb_{0.0f};
    std::atomic<float> filterEnvAmount_{0.0f};
};

} // namespace hw
