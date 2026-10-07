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
    for (EnvelopeSlot& slot : envelopes_)
        slot.curve = new EnvelopeCurve;
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
    for (EnvelopeSlot& slot : envelopes_) {
        delete slot.pendingCurve.exchange(nullptr);
        delete slot.heldCurve;
        delete slot.curve;
    }
    collectGarbage();
}

void AudioEngine::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    oscillator_.setSampleRate(sampleRate);
    frequencySmoother_.setTimeConstant(0.020, sampleRate); // pitch glides over ~20 ms
    gainSmoother_.setTimeConstant(0.005, sampleRate);      // volume changes glide over ~5 ms
    frequencySmoother_.snapTo(targetFrequency_.load());
    gainSmoother_.snapTo(volume_.load());
    for (EnvelopeSlot& slot : envelopes_)
        slot.envelope.setSampleRate(sampleRate);

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

void AudioEngine::setEnvelopeMode(int envelope, EnvelopeMode mode)
{
    envelopes_[envelope].mode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void AudioEngine::setAdsr(int envelope, const AdsrParams& params)
{
    EnvelopeSlot& slot = envelopes_[envelope];
    slot.attack.store(float(params.attack), std::memory_order_relaxed);
    slot.decay.store(float(params.decay), std::memory_order_relaxed);
    slot.sustain.store(float(params.sustain), std::memory_order_relaxed);
    slot.release.store(float(params.release), std::memory_order_relaxed);
}

void AudioEngine::setDrawnTiming(int envelope, const DrawnTiming& timing)
{
    EnvelopeSlot& slot = envelopes_[envelope];
    slot.split1.store(float(timing.split1), std::memory_order_relaxed);
    slot.split2.store(float(timing.split2), std::memory_order_relaxed);
    slot.timespan.store(float(timing.timespan), std::memory_order_relaxed);
    slot.drawnSustain.store(float(timing.sustain), std::memory_order_relaxed);
}

void AudioEngine::setEnvelopeCurve(int envelope, std::unique_ptr<EnvelopeCurve> curve)
{
    delete envelopes_[envelope].pendingCurve.exchange(curve.release(), std::memory_order_acq_rel);
    collectGarbage();
}

AudioEngine::EnvelopeStatus AudioEngine::envelopeStatus(int envelope) const
{
    const EnvelopeSlot& slot = envelopes_[envelope];
    return {static_cast<EnvelopeStage>(slot.statusStage.load(std::memory_order_relaxed)),
            slot.statusSeconds.load(std::memory_order_relaxed), slot.statusLevel.load(std::memory_order_relaxed)};
}

void AudioEngine::collectGarbage()
{
    const Wavetable* table = nullptr;
    while (retired_.pop(table))
        delete table;
    const FilterCurve* curve = nullptr;
    while (retiredCurves_.pop(curve))
        delete curve;
    const EnvelopeCurve* envelopeCurve = nullptr;
    while (retiredEnvelopeCurves_.pop(envelopeCurve))
        delete envelopeCurve;
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

void AudioEngine::updateEnvelopes()
{
    for (EnvelopeSlot& slot : envelopes_) {
        // New drawing: same hand-off as the filter curve.
        if (slot.heldCurve == nullptr)
            slot.heldCurve = slot.pendingCurve.exchange(nullptr, std::memory_order_acq_rel);
        if (slot.heldCurve != nullptr && retiredEnvelopeCurves_.push(slot.curve)) {
            slot.curve = slot.heldCurve;
            slot.heldCurve = nullptr;
        }

        Envelope& env = slot.envelope;
        env.setMode(static_cast<EnvelopeMode>(slot.mode.load(std::memory_order_relaxed)));
        env.setAdsr({slot.attack.load(std::memory_order_relaxed), slot.decay.load(std::memory_order_relaxed),
                     slot.sustain.load(std::memory_order_relaxed), slot.release.load(std::memory_order_relaxed)});
        env.setDrawn(slot.curve, {slot.split1.load(std::memory_order_relaxed), slot.split2.load(std::memory_order_relaxed),
                                  slot.timespan.load(std::memory_order_relaxed),
                                  slot.drawnSustain.load(std::memory_order_relaxed)});
    }

    // Note on / note off.
    const bool gate = gate_.load(std::memory_order_relaxed);
    if (gate != lastGate_) {
        for (EnvelopeSlot& slot : envelopes_) {
            if (gate)
                slot.envelope.gateOn();
            else
                slot.envelope.gateOff();
        }
        lastGate_ = gate;
    }
}

void AudioEngine::render(float* output, unsigned frames, unsigned channels)
{
    exchangeTables();
    exchangeFilterCurve();
    updateEnvelopes();

    frequencySmoother_.setTarget(targetFrequency_.load(std::memory_order_relaxed));
    gainSmoother_.setTarget(volume_.load(std::memory_order_relaxed));
    filterMixSmoother_.setTarget(filterEnabled_.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    shiftSmoother_.setTarget(filterShift_.load(std::memory_order_relaxed));
    resonanceXSmoother_.setTarget(resonanceX_.load(std::memory_order_relaxed));
    resonanceDbSmoother_.setTarget(resonanceDb_.load(std::memory_order_relaxed));

    for (unsigned done = 0; done < frames; done += kBlock) {
        const int count = static_cast<int>(std::min<unsigned>(kBlock, frames - done));
        renderBlock(output + static_cast<size_t>(done) * channels, count, channels);
    }

    for (EnvelopeSlot& slot : envelopes_) {
        slot.statusStage.store(static_cast<int>(slot.envelope.stage()), std::memory_order_relaxed);
        slot.statusSeconds.store(float(slot.envelope.stageSeconds()), std::memory_order_relaxed);
        slot.statusLevel.store(slot.envelope.level(), std::memory_order_relaxed);
    }
}

void AudioEngine::renderBlock(float* output, int frames, unsigned channels)
{
    Envelope& ampEnvelope = envelopes_[kAmpEnvelope].envelope;
    Envelope& filterEnvelope = envelopes_[kFilterEnvelope].envelope;

    // No note sounding: output silence and skip the oscillator and filter.
    if (ampEnvelope.stage() == EnvelopeStage::Idle && !lastGate_) {
        std::fill_n(output, static_cast<size_t>(frames) * channels, 0.0f);
        for (int i = 0; i < frames; ++i)
            filterEnvelope.next(); // keep it in step
        return;
    }

    // The filter envelope moves the cutoff once per block; it's sampled at
    // the block start and advanced through the block.
    const float filterEnvLevel = filterEnvelope.level();
    for (int i = 0; i < frames; ++i)
        filterEnvelope.next();

    for (int i = 0; i < frames; ++i) {
        oscillator_.setFrequency(frequencySmoother_.next());
        dry_[i] = oscillator_.process();
    }

    // Filter, unless it's bypassed and the crossfade out has finished.
    const bool filterAudible = filterMixSmoother_.current() > 1e-4f || filterEnabled_.load(std::memory_order_relaxed);
    if (filterAudible) {
        FilterSettings settings;
        settings.shiftOctaves = shiftSmoother_.next()
            + filterEnvAmount_.load(std::memory_order_relaxed) * filterEnvLevel;
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
        const float level = ampEnvelope.next() * gainSmoother_.next();
        const float sample = softClip((dry_[i] + (filtered - dry_[i]) * mix) * level);
        for (unsigned c = 0; c < channels; ++c)
            *output++ = sample;
    }
}

} // namespace hw
