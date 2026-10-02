#pragma once

#include "Wavetable.h"

namespace hw {

// Plays a Wavetable at a given frequency.
//
// Real-time safe: process() never allocates, locks, or blocks.
//
// Changing tables: setTable() starts a short crossfade from the old table to
// the new one, so redrawing the wave while it plays doesn't click. The
// oscillator does NOT own tables. When a crossfade ends, the old table is
// reported by finishedTable() so the owner can dispose of it, and the owner
// then calls clearFinishedTable().
class WavetableOscillator {
public:
    static constexpr double kCrossfadeSeconds = 0.010;

    explicit WavetableOscillator(double sampleRate = 48000.0);

    void setSampleRate(double sampleRate);
    void setFrequency(double frequencyHz);
    double frequency() const { return frequency_; }

    // True when a new table can be set, i.e. no previous table is still held
    // (fading out, or finished but not yet collected).
    bool canAcceptTable() const { return previous_ == nullptr; }

    // Switch to `table`. Precondition: canAcceptTable(). The first table set
    // starts immediately with no crossfade.
    void setTable(const Wavetable* table);

    // The table that was fully faded out, or nullptr if none is waiting.
    const Wavetable* finishedTable() const;
    void clearFinishedTable();

    const Wavetable* currentTable() const { return current_; }
    const Wavetable* previousTable() const { return previous_; }

    void resetPhase() { phase_ = 0.0; }

    // Produce the next output sample.
    float process();

private:
    static float read(const Wavetable& table, int level, double phase);

    double sampleRate_;
    double frequency_ = 0.0;
    double phase_ = 0.0;     // position in the cycle, 0..1
    double increment_ = 0.0; // phase advance per sample
    int level_ = 0;          // mip level for the current frequency

    const Wavetable* current_ = nullptr;
    const Wavetable* previous_ = nullptr;
    int fadeLength_ = 1;     // in samples
    int fadePosition_ = 0;
};

} // namespace hw
