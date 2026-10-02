#include "AudioEngine.h"
#include "WavetableBuilder.h"
#include "WavetableOscillator.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace hw;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::unique_ptr<Wavetable> sineTable(float sign = 1.0f)
{
    std::vector<float> points(512);
    for (int i = 0; i < 512; ++i)
        points[i] = sign * float(std::sin(2 * kPi * i / 511.0));
    return buildWavetable(points);
}

} // namespace

TEST_CASE("Oscillator plays at the requested frequency, within range")
{
    auto table = sineTable();
    WavetableOscillator osc(48000.0);
    osc.setTable(table.get());
    osc.setFrequency(440.0);

    int upwardCrossings = 0;
    float previous = osc.process();
    for (int i = 1; i < 48000; ++i) { // one second
        const float s = osc.process();
        REQUIRE(std::fabs(s) <= 1.0f);
        if (previous < 0.0f && s >= 0.0f)
            ++upwardCrossings;
        previous = s;
    }
    REQUIRE(std::abs(upwardCrossings - 440) <= 1);
}

TEST_CASE("Switching tables crossfades instead of clicking")
{
    auto a = sineTable(1.0f);
    auto b = sineTable(-1.0f); // phase-inverted: an instant switch would jump ~1.8
    WavetableOscillator osc(48000.0);
    osc.setTable(a.get());
    osc.setFrequency(100.0);

    // Advance to the peak of the cycle, where the jump would be largest.
    for (int i = 0; i < 120; ++i)
        osc.process();

    float previous = osc.process();
    REQUIRE(osc.canAcceptTable());
    osc.setTable(b.get());
    REQUIRE_FALSE(osc.canAcceptTable());

    float largestStep = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const float s = osc.process();
        largestStep = std::max(largestStep, std::fabs(s - previous));
        previous = s;
    }
    REQUIRE(largestStep < 0.05f);

    // Fade finished: the old table is reported back exactly once.
    REQUIRE(osc.finishedTable() == a.get());
    osc.clearFinishedTable();
    REQUIRE(osc.finishedTable() == nullptr);
    REQUIRE(osc.canAcceptTable());
}

TEST_CASE("Engine hands tables to the audio thread and returns them for freeing")
{
    // Drive render() directly, with no audio device, to exercise the hand-off.
    AudioEngine engine;
    engine.setGate(true);
    engine.setFrequency(220.0f);
    std::vector<float> buffer(256 * 2);

    for (int round = 0; round < 50; ++round) {
        engine.setWavetable(sineTable(round % 2 ? -1.0f : 1.0f));
        for (int block = 0; block < 4; ++block) // 4 blocks > 10 ms crossfade
            engine.render(buffer.data(), 256, 2);
        engine.collectGarbage();
    }

    bool heardSomething = false;
    for (float s : buffer) {
        REQUIRE(std::isfinite(s));
        heardSomething |= std::fabs(s) > 0.01f;
    }
    REQUIRE(heardSomething);
}
