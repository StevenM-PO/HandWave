#pragma once

#include <array>

namespace hw {

// Number of samples in one stored waveform cycle. Must be a power of two.
constexpr int kTableSize = 2048;

// One band-limited copy of the waveform per octave. Level 0 keeps every
// harmonic the table can hold (kTableSize / 2); each level after that keeps
// half as many, down to the last level, which is a pure sine (1 harmonic).
constexpr int kNumMipLevels = 11;

// A single-cycle waveform stored as a set of band-limited "mipmap" levels.
//
// Why mip levels: a hand-drawn shape usually has sharp corners, which means
// lots of high harmonics. Played at a high pitch, harmonics above Nyquist
// (sampleRate / 2) fold back as inharmonic "whistles" (aliasing). The
// oscillator avoids this by choosing the most detailed level whose top
// harmonic still fits under Nyquist at the current frequency.
//
// Wavetables are immutable once built, which is what lets the audio thread
// read one while the UI thread builds the next.
struct Wavetable {
    std::array<std::array<float, kTableSize>, kNumMipLevels> levels{};

    // Highest harmonic number present in `level`.
    static constexpr int maxHarmonic(int level) { return (kTableSize / 2) >> level; }

    // Most detailed level that will not alias when played at `frequencyHz`.
    static int levelForFrequency(double frequencyHz, double sampleRate)
    {
        if (frequencyHz <= 0.0)
            return 0;
        // How many harmonics of this fundamental fit below Nyquist.
        const double harmonicsThatFit = (sampleRate * 0.5) / frequencyHz;
        int level = 0;
        while (level < kNumMipLevels - 1 && maxHarmonic(level) > harmonicsThatFit)
            ++level;
        return level;
    }
};

} // namespace hw
