#pragma once

#include <complex>
#include <vector>

namespace hw {

// In-place iterative radix-2 FFT.
//
// data.size() must be a power of two. When `inverse` is true the inverse
// transform is computed WITHOUT the 1/N scale; the caller divides by N.
//
// This is deliberately small and simple: it only runs when a wavetable is
// rebuilt (a handful of 2048-point transforms), never on the audio thread.
void fft(std::vector<std::complex<double>>& data, bool inverse);

} // namespace hw
