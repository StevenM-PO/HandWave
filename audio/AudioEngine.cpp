#include "AudioEngine.h"

#include "miniaudio.h"

namespace hw {

namespace {

constexpr ma_uint32 kPreferredSampleRate = 48000;
constexpr ma_uint32 kPeriodFrames = 256; // ~5 ms at 48 kHz
constexpr ma_uint32 kChannels = 2;

void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames)
{
    auto* engine = static_cast<AudioEngine*>(device->pUserData);
    engine->render(static_cast<float*>(output), frames, device->playback.channels);
}

} // namespace

AudioEngine::AudioEngine()
{
    prepare(sampleRate_);
}

AudioEngine::~AudioEngine()
{
    stop();
    // The audio thread is gone, so everything can be freed from here.
    delete pending_.exchange(nullptr);
    delete oscillator_.previousTable();
    delete oscillator_.currentTable();
    collectGarbage();
}

void AudioEngine::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    oscillator_.setSampleRate(sampleRate);
    frequencySmoother_.setTimeConstant(0.020, sampleRate); // pitch glides over ~20 ms
    gainSmoother_.setTimeConstant(0.005, sampleRate);      // fades in/out over ~5 ms
    frequencySmoother_.snapTo(targetFrequency_.load());
    gainSmoother_.snapTo(0.0f);
}

bool AudioEngine::start()
{
    if (running_)
        return true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = kPreferredSampleRate;
    config.periodSizeInFrames = kPeriodFrames;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dataCallback;
    config.pUserData = this;

    device_ = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &config, device_.get()) != MA_SUCCESS) {
        lastError_ = "Could not open the audio output device.";
        device_.reset();
        return false;
    }

    // The backend may not honour our preferred rate; use what we actually got.
    prepare(static_cast<double>(device_->sampleRate));

    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        lastError_ = "Could not start the audio output device.";
        ma_device_uninit(device_.get());
        device_.reset();
        return false;
    }

    running_ = true;
    lastError_.clear();
    return true;
}

void AudioEngine::stop()
{
    if (!device_)
        return;
    ma_device_uninit(device_.get()); // blocks until the audio thread has stopped
    device_.reset();
    running_ = false;
}

void AudioEngine::setWavetable(std::unique_ptr<Wavetable> table)
{
    // If the audio thread hasn't picked up the previous pending table yet, it
    // never will. exchange() hands it back to us, so we can free it here.
    delete pending_.exchange(table.release(), std::memory_order_acq_rel);
    collectGarbage();
}

void AudioEngine::collectGarbage()
{
    const Wavetable* table = nullptr;
    while (retired_.pop(table))
        delete table;
}

void AudioEngine::exchangeTables()
{
    // Hand back a table that has fully faded out. If the queue is full (the UI
    // thread isn't collecting), keep it and try again next block.
    if (const Wavetable* finished = oscillator_.finishedTable()) {
        if (retired_.push(finished))
            oscillator_.clearFinishedTable();
    }

    // Take a newly drawn table, unless a crossfade is still in progress. In
    // that case it waits in the mailbox for a few milliseconds.
    if (oscillator_.canAcceptTable()) {
        if (Wavetable* next = pending_.exchange(nullptr, std::memory_order_acq_rel))
            oscillator_.setTable(next);
    }
}

void AudioEngine::render(float* output, unsigned frames, unsigned channels)
{
    exchangeTables();

    frequencySmoother_.setTarget(targetFrequency_.load(std::memory_order_relaxed));
    const bool gate = gate_.load(std::memory_order_relaxed);
    gainSmoother_.setTarget(gate ? volume_.load(std::memory_order_relaxed) : 0.0f);

    for (unsigned i = 0; i < frames; ++i) {
        oscillator_.setFrequency(frequencySmoother_.next());
        const float sample = oscillator_.process() * gainSmoother_.next();
        for (unsigned c = 0; c < channels; ++c)
            *output++ = sample;
    }
}

} // namespace hw
