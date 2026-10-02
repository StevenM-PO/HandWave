#include "Fft.h"
#include "WavetableBuilder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

using namespace hw;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Drawing in the same convention as WaveCanvas: first point = start of cycle,
// last point = end of cycle.
std::vector<float> drawShape(int count, float (*f)(double t))
{
    std::vector<float> points(count);
    for (int i = 0; i < count; ++i)
        points[i] = f(static_cast<double>(i) / (count - 1));
    return points;
}

// Magnitude of each harmonic 0..N/2 in one table level.
std::vector<double> harmonics(const std::array<float, kTableSize>& level)
{
    std::vector<std::complex<double>> data(level.begin(), level.end());
    fft(data, false);
    std::vector<double> mags(kTableSize / 2 + 1);
    for (int h = 0; h <= kTableSize / 2; ++h)
        mags[h] = std::abs(data[h]) / kTableSize;
    return mags;
}

} // namespace

TEST_CASE("A drawn sine contains only the fundamental")
{
    auto table = buildWavetable(drawShape(512, [](double t) { return float(std::sin(2 * kPi * t)); }));
    auto mags = harmonics(table->levels[0]);

    double total = 0.0;
    for (int h = 1; h < (int)mags.size(); ++h)
        total += mags[h] * mags[h];
    REQUIRE(mags[1] * mags[1] / total > 0.999);
}

TEST_CASE("Each mip level has nothing above its harmonic limit")
{
    auto table = buildWavetable(drawShape(512, [](double t) { return float(2 * t - 1); })); // saw
    for (int level = 0; level < kNumMipLevels; ++level) {
        auto mags = harmonics(table->levels[level]);
        const int keep = Wavetable::maxHarmonic(level);
        for (int h = keep + 1; h <= kTableSize / 2; ++h)
            REQUIRE(mags[h] < 1e-5);
        // The fundamental must survive at every level.
        REQUIRE(mags[1] > 0.1);
    }
}

TEST_CASE("DC offset is removed and the peak is normalized")
{
    // Entirely positive drawing: a raised half-wave bump.
    auto table = buildWavetable(drawShape(512, [](double t) { return float(0.5 + 0.3 * std::sin(2 * kPi * t)); }));

    float peak = 0.0f;
    for (const auto& level : table->levels) {
        double sum = 0.0;
        for (float s : level) {
            sum += s;
            peak = std::max(peak, std::fabs(s));
        }
        REQUIRE(std::fabs(sum / kTableSize) < 1e-5);
    }
    REQUIRE(peak == Catch::Approx(kWavetablePeak).margin(1e-5));
}

TEST_CASE("A flat or empty drawing gives silence, not noise or NaN")
{
    for (const auto& points : {std::vector<float>(512, 0.0f), std::vector<float>(512, 0.7f),
                               std::vector<float>{}}) {
        auto table = buildWavetable(points);
        for (const auto& level : table->levels)
            for (float s : level)
                REQUIRE(s == 0.0f);
    }
}

TEST_CASE("Mip level selection avoids aliasing")
{
    const double sr = 48000.0;
    for (double f : {20.0, 55.0, 110.0, 440.0, 1000.0, 3000.0, 8000.0}) {
        const int level = Wavetable::levelForFrequency(f, sr);
        INFO("frequency " << f << " level " << level);
        REQUIRE(Wavetable::maxHarmonic(level) * f <= sr / 2);
        // ...and isn't needlessly dull: the next more detailed level would alias.
        if (level > 0)
            REQUIRE(Wavetable::maxHarmonic(level - 1) * f > sr / 2);
    }
}
