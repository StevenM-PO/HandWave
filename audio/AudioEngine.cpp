#include "AudioEngine.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>

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

// Transparent up to kKnee, then rounds off smoothly toward full scale, so
// filter boosts and resonance saturate instead of hard-clipping.
float softClip(float x)
{
    constexpr float kKnee = 0.9f;
    const float a = std::fabs(x);
    if (a <= kKnee)
        return x;
    const float y = kKnee + (1.0f - kKnee) * std::tanh((a - kKnee) / (1.0f - kKnee));
    return std::copysign(y, x);
}

} // namespace

AudioEngine::AudioEngine()
{
    filterCurve_ = new FilterCurve(FilterCurve::preset(FilterPreset::Flat));
    prepare(sampleRate_);
}

AudioEngine::~AudioEngine()
{
    stop();
    // The audio thread is gone, so everything can be freed from here.
    delete pending_.exchange(nullptr);
    delete oscillator_.previousTable();
    delete oscillator_.currentTable();
    delete pendingCurve_.exchange(nullptr);
    delete heldCurve_;
    delete filterCurve_;
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

    // Builds the filter's design matrices: done here, never on the audio thread.
    filter_ = std::make_unique<DrawnFilter>(sampleRate);
    const double blockRate = sampleRate / kBlock;
    shiftSmoother_.setTimeConstant(0.030, blockRate);
    resonanceXSmoother_.setTimeConstant(0.030, blockRate);
    resonanceDbSmoother_.setTimeConstant(0.030, blockRate);
    shiftSmoother_.snapTo(filterShift_.load());
    resonanceXSmoother_.snapTo(resonanceX_.load());
    resonanceDbSmoother_.snapTo(resonanceDb_.load());
    filterMixSmoother_.setTimeConstant(0.010, sampleRate);
    filterMixSmoother_.snapTo(filterEnabled_.load() ? 1.0f : 0.0f);
}

bool AudioEngine::start()
{
    if (running_)
        return true;

    // Ask for a real-time audio thread (SCHED_FIFO on Linux) so UI work can't
    // starve it. If the OS refuses, miniaudio quietly falls back to normal.
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.threadPriority = ma_thread_priority_realtime;
    context_ = std::make_unique<ma_context>();
    if (ma_context_init(nullptr, 0, &contextConfig, context_.get()) != MA_SUCCESS) {
        lastError_ = "Could not initialize the audio system.";
        context_.reset();
        return false;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = kPreferredSampleRate;
    config.periodSizeInFrames = kPeriodFrames;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dataCallback;
    config.pUserData = this;

    device_ = std::make_unique<ma_device>();
    if (ma_device_init(context_.get(), &config, device_.get()) != MA_SUCCESS) {
        lastError_ = "Could not open the audio output device.";
        device_.reset();
        ma_context_uninit(context_.get());
        context_.reset();
        return false;
    }

    // The backend may not honour our preferred rate; use what we actually got.
    prepare(static_cast<double>(device_->sampleRate));

    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        lastError_ = "Could not start the audio output device.";
        stop();
        return false;
    }

    running_ = true;
    lastError_.clear();
    return true;
}

void AudioEngine::stop()
{
    if (device_) {
        ma_device_uninit(device_.get()); // blocks until the audio thread has stopped
        device_.reset();
    }
    if (context_) {
        ma_context_uninit(context_.get());
        context_.reset();
    }
    running_ = false;
}

void AudioEngine::setWavetable(std::unique_ptr<Wavetable> table)
{
    // If the audio thread hasn't picked up the previous pending table yet, it
    // never will. exchange() hands it back to us, so we can free it here.
    delete pending_.exchange(table.release(), std::memory_order_acq_rel);
    collectGarbage();
}

void AudioEngine::setFilterCurve(std::unique_ptr<FilterCurve> curve)
{
    delete pendingCurve_.exchange(curve.release(), std::memory_order_acq_rel);
    collectGarbage();
}

void AudioEngine::collectGarbage()
{
    const Wavetable* table = nullptr;
    while (retired_.pop(table))
        delete table;
    const FilterCurve* curve = nullptr;
    while (retiredCurves_.pop(curve))
        delete curve;
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

void AudioEngine::exchangeFilterCurve()
{
    // The filter copies what it needs from the curve during design, and blends
    // the coefficient change in itself, so a new curve can be swapped in at once.
    // Taking it out of the mailbox makes it ours; if the old curve can't be
    // handed back yet (queue full), hold the new one until next block.
    if (heldCurve_ == nullptr)
        heldCurve_ = pendingCurve_.exchange(nullptr, std::memory_order_acq_rel);
    if (heldCurve_ != nullptr && retiredCurves_.push(filterCurve_)) {
        filterCurve_ = heldCurve_;
        heldCurve_ = nullptr;
        filter_->curveChanged();
    }
}

void AudioEngine::render(float* output, unsigned frames, unsigned channels)
{
    exchangeTables();
    exchangeFilterCurve();

    frequencySmoother_.setTarget(targetFrequency_.load(std::memory_order_relaxed));
    const bool gate = gate_.load(std::memory_order_relaxed);
    gainSmoother_.setTarget(gate ? volume_.load(std::memory_order_relaxed) : 0.0f);
    filterMixSmoother_.setTarget(filterEnabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    shiftSmoother_.setTarget(filterShift_.load(std::memory_order_relaxed));
    resonanceXSmoother_.setTarget(resonanceX_.load(std::memory_order_relaxed));
    resonanceDbSmoother_.setTarget(resonanceDb_.load(std::memory_order_relaxed));

    for (unsigned done = 0; done < frames; done += kBlock) {
        const int count = static_cast<int>(std::min<unsigned>(kBlock, frames - done));
        renderBlock(output + static_cast<size_t>(done) * channels, count, channels);
    }
}

void AudioEngine::renderBlock(float* output, int frames, unsigned channels)
{
    for (int i = 0; i < frames; ++i) {
        oscillator_.setFrequency(frequencySmoother_.next());
        dry_[i] = oscillator_.process();
    }

    // Filter, unless it's bypassed and the crossfade out has finished.
    const bool filterAudible = filterMixSmoother_.current() > 1e-4f || filterEnabled_.load(std::memory_order_relaxed);
    if (filterAudible) {
        FilterSettings settings;
        settings.shiftOctaves = shiftSmoother_.next();
        settings.loop = filterLoop_.load(std::memory_order_relaxed);
        settings.resonanceX = resonanceXSmoother_.next();
        settings.resonanceDb = resonanceDbSmoother_.next();
        filter_->update(*filterCurve_, settings);
        std::copy_n(dry_.begin(), frames, wet_.begin());
        filter_->process(wet_.data(), frames);
    } else {
        filter_->reset(); // start clean when it's switched back on
    }

    for (int i = 0; i < frames; ++i) {
        const float mix = filterMixSmoother_.next();
        const float filtered = filterAudible ? wet_[i] : dry_[i];
        const float sample = softClip((dry_[i] + (filtered - dry_[i]) * mix) * gainSmoother_.next());
        for (unsigned c = 0; c < channels; ++c)
            *output++ = sample;
    }
}

} // namespace hw
