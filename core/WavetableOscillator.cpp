#include "WavetableOscillator.h"

#include <algorithm>
#include <cassert>

namespace hw {

WavetableOscillator::WavetableOscillator(double sampleRate)
    : sampleRate_(sampleRate)
{
    setSampleRate(sampleRate);
}

void WavetableOscillator::setSampleRate(double sampleRate)
{
    sampleRate_ = sampleRate;
    fadeLength_ = std::max(1, static_cast<int>(kCrossfadeSeconds * sampleRate));
    setFrequency(frequency_);
}

void WavetableOscillator::setFrequency(double frequencyHz)
{
    frequency_ = frequencyHz;
    increment_ = frequencyHz / sampleRate_;
    level_ = Wavetable::levelForFrequency(frequencyHz, sampleRate_);
}

void WavetableOscillator::setTable(const Wavetable* table)
{
    assert(canAcceptTable());
    if (current_ == nullptr) {
        current_ = table;
        return;
    }
    previous_ = current_;
    current_ = table;
    fadePosition_ = 0;
}

const Wavetable* WavetableOscillator::finishedTable() const
{
    return (previous_ != nullptr && fadePosition_ >= fadeLength_) ? previous_ : nullptr;
}

void WavetableOscillator::clearFinishedTable()
{
    if (finishedTable() != nullptr)
        previous_ = nullptr;
}

float WavetableOscillator::process()
{
    if (current_ == nullptr)
        return 0.0f;

    float out = read(*current_, level_, phase_);

    if (previous_ != nullptr && fadePosition_ < fadeLength_) {
        const float mix = static_cast<float>(fadePosition_) / static_cast<float>(fadeLength_);
        const float old = read(*previous_, level_, phase_);
        out = old + (out - old) * mix;
        ++fadePosition_;
    }

    phase_ += increment_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    return out;
}

float WavetableOscillator::read(const Wavetable& table, int level, double phase)
{
    // Linear interpolation between neighbouring samples. Band-limiting already
    // removed the content that cheap interpolation handles badly.
    const auto& samples = table.levels[level];
    const double position = phase * kTableSize;
    const int i0 = static_cast<int>(position) & (kTableSize - 1);
    const int i1 = (i0 + 1) & (kTableSize - 1);
    const float frac = static_cast<float>(position - static_cast<int>(position));
    return samples[i0] + (samples[i1] - samples[i0]) * frac;
}

} // namespace hw
