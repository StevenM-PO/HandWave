#include "WavetableBuilder.h"

#include "Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace hw {

std::unique_ptr<Wavetable> buildWavetable(const std::vector<float>& points)
{
    auto table = std::make_unique<Wavetable>(); // all levels start zeroed
    if (points.size() < 2)
        return table;

    // 1. Resample the drawing to one cycle of kTableSize samples.
    std::vector<std::complex<double>> spectrum(kTableSize);
    const double span = static_cast<double>(points.size() - 1);
    for (int j = 0; j < kTableSize; ++j) {
        const double x = static_cast<double>(j) / kTableSize * span;
        const size_t i = static_cast<size_t>(x); // x < span, so i + 1 is valid
        const double frac = x - static_cast<double>(i);
        const double y = points[i] + (points[i + 1] - points[i]) * frac;
        spectrum[j] = std::complex<double>(y, 0.0);
    }

    // 2. To the frequency domain. Bin h holds harmonic h (and bin N-h its mirror).
    fft(spectrum, false);
    spectrum[0] = 0.0;              // remove DC offset
    spectrum[kTableSize / 2] = 0.0; // Nyquist bin: no usable phase, always drop

    // 3. Build each band-limited level.
    std::vector<std::complex<double>> work(kTableSize);
    for (int level = 0; level < kNumMipLevels; ++level) {
        const int keep = Wavetable::maxHarmonic(level);
        work = spectrum;
        // Zero harmonics keep+1 .. N/2 and their mirrored negative frequencies.
        for (int h = keep + 1; h < kTableSize - keep; ++h)
            work[h] = 0.0;
        fft(work, true);
        auto& out = table->levels[level];
        for (int j = 0; j < kTableSize; ++j)
            out[j] = static_cast<float>(work[j].real() / kTableSize);
    }

    // 4. Normalize all levels with one shared gain.
    float peak = 0.0f;
    for (const auto& level : table->levels)
        for (float s : level)
            peak = std::max(peak, std::fabs(s));

    if (peak < 1e-6f) {
        // Flat drawing: nothing to hear. Return exact silence, not amplified noise.
        for (auto& level : table->levels)
            level.fill(0.0f);
        return table;
    }

    const float gain = kWavetablePeak / peak;
    for (auto& level : table->levels)
        for (float& s : level)
            s *= gain;

    return table;
}

} // namespace hw
