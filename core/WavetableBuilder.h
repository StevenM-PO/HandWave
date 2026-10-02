#pragma once

#include "Wavetable.h"

#include <memory>
#include <vector>

namespace hw {

// Peak level that built tables are normalized to (leaves a little headroom).
constexpr float kWavetablePeak = 0.9f;

// Turns a hand-drawn shape into a playable, band-limited Wavetable.
//
// `points` are y-values (nominally -1..1) evenly spaced across exactly one
// cycle: points.front() is at the start of the cycle and points.back() at the
// end. Any number of points >= 2 works; fewer gives a silent table.
//
// Steps:
//   1. Resample the drawing to kTableSize samples (linear interpolation).
//   2. FFT, then drop the DC (0 Hz) offset so the speaker cone stays centred.
//   3. For each mip level, zero the harmonics above that level's limit and
//      inverse-FFT back to a waveform.
//   4. Normalize every level by the same gain so the loudest peak is
//      kWavetablePeak (consistent volume whatever the user drew).
//
// Note that a jump between the end and the start of the drawing (e.g. a saw)
// is kept on purpose. It is part of the timbre, and band-limiting stops it
// from aliasing.
std::unique_ptr<Wavetable> buildWavetable(const std::vector<float>& points);

} // namespace hw
