#include "AudioEngine.h"
#include "DrawnFilter.h"
#include "WavetableBuilder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace hw;

namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Largest |achieved - target| (dB) over the audible display range, only
// counting places where the target is above `floorDb` (deep stop-bands are
// allowed to be approximate).
double worstError(const FilterDesigner& designer, const FilterCurve& curve, const FilterSettings& settings,
                  double floorDb, double* atHz = nullptr)
{
    FilterDesigner::Sections sections;
    FilterSettings noResonance = settings;
    noResonance.resonanceDb = targetDb(curve, settings, *curveToDisplay(settings.resonanceX, settings));
    designer.design(curve, noResonance, sections);

    double worst = 0.0;
    for (int i = 0; i <= 400; ++i) {
        const double x = filterHzToX(25.0) + (filterHzToX(16000.0) - filterHzToX(25.0)) * i / 400.0;
        const double target = targetDb(curve, settings, x);
        if (target < floorDb)
            continue;
        const double err = std::fabs(designer.responseDb(sections, filterXToHz(x)) - target);
        if (err > worst) {
            worst = err;
            if (atHz)
                *atHz = filterXToHz(x);
        }
    }
    return worst;
}

// RMS level (dB) of a sine at `hz` after the filter, relative to its input.
double measuredGainDb(DrawnFilter& filter, double hz)
{
    filter.reset();
    std::vector<float> buf(9600);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = float(std::sin(2 * kPi * hz * i / kRate));
    filter.process(buf.data(), int(buf.size()));
    double sum = 0.0;
    const size_t start = buf.size() / 2; // skip the settling time
    for (size_t i = start; i < buf.size(); ++i)
        sum += double(buf[i]) * buf[i];
    const double rms = std::sqrt(sum / (buf.size() - start));
    return 20.0 * std::log10(rms / std::sqrt(0.5));
}

} // namespace

TEST_CASE("Display <-> frequency mapping spans 20 Hz to 20 kHz")
{
    REQUIRE(filterXToHz(0.0) == Catch::Approx(20.0));
    REQUIRE(filterXToHz(1.0) == Catch::Approx(20000.0));
    REQUIRE(filterHzToX(filterXToHz(0.37)) == Catch::Approx(0.37));
}

TEST_CASE("Cutoff shift extends the ends without loop and wraps with it")
{
    FilterCurve curve = FilterCurve::preset(FilterPreset::LowPass);
    FilterSettings s;
    s.shiftOctaves = -3.0; // slide the low-pass down: high end comes in from the right

    // Without loop, the right edge of the display shows the curve's last value...
    REQUIRE(targetDb(curve, s, 1.0) == Catch::Approx(curve.db.back()));
    REQUIRE_FALSE(displayToCurve(0.99, s).has_value()); // past the end of the curve
    s.shiftOctaves = 3.0;
    // ...and the left edge, shifted past the start, extends the first value.
    REQUIRE(targetDb(curve, s, 0.0) == Catch::Approx(curve.db.front()));
    REQUIRE_FALSE(displayToCurve(0.05, s).has_value());

    // With loop, the far end of the curve wraps into view.
    s.loop = true;
    const double back = s.shiftOctaves / filterOctaveSpan();
    REQUIRE(targetDb(curve, s, 0.0) == Catch::Approx(curve.sample(1.0 - back)));
    REQUIRE(displayToCurve(0.05, s).has_value());
}

TEST_CASE("A flat curve passes everything at 0 dB")
{
    FilterDesigner designer(kRate);
    FilterSettings s;
    s.resonanceDb = 0.0;
    REQUIRE(worstError(designer, FilterCurve::preset(FilterPreset::Flat), s, -100.0) < 0.5);
}

TEST_CASE("Presets are matched closely where they pass sound")
{
    FilterDesigner designer(kRate);
    for (auto preset : {FilterPreset::LowPass, FilterPreset::HighPass, FilterPreset::BandPass}) {
        FilterSettings s;
        FilterCurve curve = FilterCurve::preset(preset, &s.resonanceX);
        double at = 0.0;
        const double err = worstError(designer, curve, s, -30.0, &at);
        INFO("preset " << int(preset) << " worst error " << err << " dB at " << at << " Hz");
        REQUIRE(err < 3.0);
    }
}

TEST_CASE("Resonance reaches its level at its point and moves with the cutoff")
{
    FilterDesigner designer(kRate);
    FilterSettings s;
    FilterCurve curve = FilterCurve::preset(FilterPreset::LowPass, &s.resonanceX);
    FilterDesigner::Sections sections;

    for (double shift : {0.0, -2.0, 1.5}) {
        s.shiftOctaves = shift;
        s.resonanceDb = 12.0;
        designer.design(curve, s, sections);
        const double hz = filterXToHz(*curveToDisplay(s.resonanceX, s));
        INFO("shift " << shift << " resonance at " << hz << " Hz");
        REQUIRE(hz == Catch::Approx(1000.0 * std::exp2(shift)).epsilon(0.001));
        REQUIRE(designer.responseDb(sections, hz) == Catch::Approx(12.0).margin(1.5));
        // It's a narrow peak: an octave away the response is back near the curve.
        REQUIRE(designer.responseDb(sections, hz / 2) < 3.0);
    }
}

TEST_CASE("The running filter matches its design")
{
    DrawnFilter filter(kRate);
    FilterSettings s;
    FilterCurve curve = FilterCurve::preset(FilterPreset::LowPass, &s.resonanceX);
    s.resonanceDb = curve.sample(s.resonanceX);
    filter.update(curve, s);

    FilterDesigner::Sections sections;
    filter.designer().design(curve, s, sections);
    for (double hz : {100.0, 700.0, 1500.0, 3000.0}) {
        INFO(hz << " Hz");
        const double expected = filter.designer().responseDb(sections, hz) + filter.levelDb();
        REQUIRE(measuredGainDb(filter, hz) == Catch::Approx(expected).margin(0.5));
    }
}

TEST_CASE("Auto-level keeps boosts from overloading")
{
    DrawnFilter filter(kRate);
    FilterSettings s;
    FilterCurve curve = FilterCurve::preset(FilterPreset::LowPass, &s.resonanceX);

    // No boost anywhere: the level is left alone.
    s.resonanceDb = curve.sample(s.resonanceX);
    filter.update(curve, s);
    REQUIRE(filter.levelDb() > -1.0);

    // Big resonance: the peak comes out at about unity, not +24 dB.
    s.resonanceDb = 24.0;
    filter.update(curve, s);
    filter.process(std::vector<float>(512).data(), 512); // let the level blend in
    REQUIRE(filter.levelDb() < -20.0);
    REQUIRE(measuredGainDb(filter, 1000.0) == Catch::Approx(0.0).margin(1.0));

    // A curve drawn +12 dB everywhere is levelled back so its loudest point
    // is at unity (the rest sits within the design's ripple below it).
    FilterCurve loud;
    std::fill(loud.db.begin(), loud.db.end(), 12.0f);
    FilterSettings flat;
    flat.resonanceDb = 12.0;
    filter.update(loud, flat);
    filter.process(std::vector<float>(512).data(), 512);
    FilterDesigner::Sections sections;
    filter.designer().design(loud, flat, sections);
    REQUIRE(filter.designer().peakDb(sections, flat) + filter.levelDb() == Catch::Approx(0.0).margin(0.01));
    const double expected = filter.designer().responseDb(sections, 440.0) + filter.levelDb();
    REQUIRE(expected <= 0.01);
    REQUIRE(measuredGainDb(filter, 440.0) == Catch::Approx(expected).margin(0.5));
}

TEST_CASE("Fast cutoff and resonance sweeps stay stable")
{
    DrawnFilter filter(kRate);
    FilterSettings s;
    FilterCurve curve = FilterCurve::preset(FilterPreset::BandPass, &s.resonanceX);

    std::vector<float> block(256);
    float peak = 0.0f;
    for (int b = 0; b < 400; ++b) {
        // Sweep 8 octaves up and down every ~1 s, resonance pumping up to +24 dB.
        s.shiftOctaves = 4.0 * std::sin(b * 0.03);
        s.resonanceDb = 12.0 + 12.0 * std::sin(b * 0.11);
        s.loop = (b / 50) % 2 == 1;
        filter.update(curve, s);
        for (int i = 0; i < 256; ++i)
            block[i] = (b == 0 && i == 0) ? 1.0f : 0.1f * float(std::sin(i * 0.37 + b));
        filter.process(block.data(), 256);
        for (float v : block) {
            REQUIRE(std::isfinite(v));
            peak = std::max(peak, std::fabs(v));
        }
    }
    REQUIRE(peak < 20.0f); // bounded: +24 dB on a 0.1 input is ~1.6
}

TEST_CASE("Band width tuning report", "[.diag]")
{
    // Run with: handwave_tests "[.diag]"  (not part of the normal suite)
    for (double q : {0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.8}) {
        FilterDesigner designer(kRate, q);
        double worst30 = 0.0, worst48 = 0.0;
        for (auto preset : {FilterPreset::LowPass, FilterPreset::HighPass, FilterPreset::BandPass}) {
            FilterSettings s;
            FilterCurve curve = FilterCurve::preset(preset, &s.resonanceX);
            worst30 = std::max(worst30, worstError(designer, curve, s, -30.0));
            worst48 = std::max(worst48, worstError(designer, curve, s, -48.0));
        }
        // Hand-drawn-like wiggles: +-12 dB with periods of ~1.3 and ~2 octaves.
        double zigErr = 0.0;
        for (double period : {68.0, 103.0}) {
            FilterCurve zig;
            for (int i = 0; i < kFilterCurvePoints; ++i)
                zig.db[i] = float(12.0 * std::sin(i * 2 * kPi / period));
            zigErr = std::max(zigErr, worstError(designer, zig, FilterSettings{}, -100.0));
        }
        WARN("Q " << q << ": presets worst " << worst30 << " dB above -30 dB, " << worst48
                  << " dB overall; zig-zag " << zigErr << " dB");
    }
}

TEST_CASE("Engine runs the filter, swaps curves, and frees the old ones")
{
    // Drive render() directly, with no audio device.
    AudioEngine engine;
    std::vector<float> wave(512);
    for (int i = 0; i < 512; ++i)
        wave[i] = 2.0f * i / 511.0f - 1.0f; // saw: lots of harmonics to filter
    engine.setWavetable(buildWavetable(wave));
    engine.setGate(true);
    engine.setVolume(1.0f);

    std::vector<float> out(1024 * 2);
    for (int round = 0; round < 40; ++round) {
        auto preset = round % 2 ? FilterPreset::LowPass : FilterPreset::HighPass;
        engine.setFilterCurve(std::make_unique<FilterCurve>(FilterCurve::preset(preset)));
        engine.setFilterCutoff(float(round % 7) - 3.0f);
        engine.setFilterLoop(round % 3 == 0);
        engine.setResonance(0.57f, 18.0f);
        engine.setFilterEnabled(round % 5 != 4);
        for (int block = 0; block < 4; ++block)
            engine.render(out.data(), 1024, 2);
        engine.collectGarbage();
        for (float s : out) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) <= 1.0f); // soft clip keeps it in range
        }
    }
}
