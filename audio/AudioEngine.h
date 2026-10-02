#pragma once

#include "ParamSmoother.h"
#include "SpscQueue.h"
#include "Wavetable.h"
#include "WavetableOscillator.h"

#include <atomic>
#include <memory>
#include <string>

struct ma_device; // from miniaudio.h, kept out of this header

namespace hw {

// Owns the audio output device and the synth voice.
//
// Threading model:
//   - The "control" thread (the UI thread) calls start/stop and the setters.
//   - miniaudio's real-time thread calls render(). It must never block or
//     allocate, so all communication is via atomics and lock-free queues.
//
// Wavetable hand-off:
//   UI thread builds a table --> pending_ (atomic mailbox) --> audio thread
//   audio thread finishes with the old table --> retired_ queue --> UI thread
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

    // Free tables the audio thread has finished with. Call regularly.
    void collectGarbage();

    // ---- Audio thread only ----
    // Fill `frames` interleaved frames of `channels` channels.
    void render(float* output, unsigned frames, unsigned channels);

private:
    void prepare(double sampleRate);
    void exchangeTables();

    std::unique_ptr<ma_device> device_;
    bool running_ = false;
    std::string lastError_;
    double sampleRate_ = 48000.0;

    // Owned by the audio thread while running.
    WavetableOscillator oscillator_;
    ParamSmoother frequencySmoother_;
    ParamSmoother gainSmoother_;

    std::atomic<Wavetable*> pending_{nullptr};
    SpscQueue<const Wavetable*, 64> retired_;

    std::atomic<float> targetFrequency_{110.0f};
    std::atomic<float> volume_{0.5f};
    std::atomic<bool> gate_{false};
};

} // namespace hw
