#include "AudioEngine.h"
#include "Envelope.h"
#include "WavetableBuilder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace hw;

namespace {

constexpr double kRate = 48000.0;

// Run the envelope for `seconds`, returning every sample.
std::vector<float> run(Envelope& env, double seconds)
{
    std::vector<float> out(static_cast<size_t>(seconds * kRate));
    for (float& v : out)
        v = env.next();
    return out;
}

float largestStep(const std::vector<float>& v, float previous)
{
    float largest = 0.0f;
    for (float x : v) {
        largest = std::max(largest, std::fabs(x - previous));
        previous = x;
    }
    return largest;
}

EnvelopeCurve drawnCurve(float (*f)(double x))
{
    EnvelopeCurve c;
    for (int i = 0; i < kEnvelopePoints; ++i)
        c.points[i] = f(double(i) / (kEnvelopePoints - 1));
    return c;
}

} // namespace

TEST_CASE("ADSR hits its levels at the right times")
{
    Envelope env(kRate);
    AdsrParams p;
    p.attack = 0.1;
    p.decay = 0.2;
    p.sustain = 0.5;
    p.release = 0.3;
    env.setAdsr(p);

    env.gateOn();
    auto attack = run(env, 0.1);
    REQUIRE(attack.back() == Catch::Approx(1.0f).margin(0.01));
    REQUIRE(env.stage() == EnvelopeStage::Attack);
    auto decay = run(env, 0.21);
    REQUIRE(decay.back() == Catch::Approx(0.5f).margin(0.01));
    REQUIRE(env.stage() == EnvelopeStage::Sustain);
    run(env, 0.5);
    REQUIRE(env.level() == Catch::Approx(0.5f).margin(0.001));

    env.gateOff();
    auto release = run(env, 0.32);
    REQUIRE(release.back() == 0.0f);
    REQUIRE(env.stage() == EnvelopeStage::Idle);
}

TEST_CASE("Early release and retrigger never jump")
{
    Envelope env(kRate);
    AdsrParams p;
    p.attack = 0.2;
    p.release = 0.5;
    env.setAdsr(p);

    env.gateOn();
    auto a = run(env, 0.1); // halfway through the attack
    env.gateOff();
    auto r = run(env, 0.2); // halfway through the release
    env.gateOn();           // retrigger from wherever it is
    auto a2 = run(env, 0.3);

    // Per-sample changes stay tiny: no clicks at note-off or retrigger.
    REQUIRE(largestStep(r, a.back()) < 0.001f);
    REQUIRE(largestStep(a2, r.back()) < 0.002f);
    REQUIRE(a2.front() >= r.back() - 0.001f); // continues up from the release level
}

TEST_CASE("Drawn segment times are fractions of the timespan")
{
    DrawnTiming t;
    t.timespan = 9.0;
    t.split1 = 1.0 / 3.0;
    t.split2 = 2.0 / 3.0;
    REQUIRE(t.attackSeconds() == Catch::Approx(3.0));
    REQUIRE(t.decaySeconds() == Catch::Approx(3.0));
    REQUIRE(t.releaseSeconds() == Catch::Approx(3.0));
}

TEST_CASE("Drawn mode follows the drawing, sustaining where the decay ends")
{
    // Rises over the first third, wiggles down to 0.4, falls to 0.
    EnvelopeCurve curve = drawnCurve([](double x) {
        if (x < 1.0 / 3.0)
            return float(3.0 * x);
        if (x < 2.0 / 3.0)
            return float(1.0 - 0.6 * (x - 1.0 / 3.0) * 3.0 + 0.05 * std::sin((x - 1.0 / 3.0) * 6 * 3.14159265));
        return float(0.4 * (1.0 - (x - 2.0 / 3.0) * 3.0));
    });
    DrawnTiming t;
    t.timespan = 0.3; // 100 ms per segment
    const float sustain = curve.sample(t.split2);
    REQUIRE(sustain == Catch::Approx(0.4f).margin(0.01));

    Envelope env(kRate);
    env.setMode(EnvelopeMode::Drawn);
    env.setDrawn(&curve, t);
    env.gateOn();

    auto attack = run(env, 0.1);
    REQUIRE(attack[attack.size() / 2] == Catch::Approx(curve.sample(1.0 / 6.0)).margin(0.02));
    REQUIRE(attack.back() == Catch::Approx(1.0f).margin(0.02));

    auto decay = run(env, 0.101);
    REQUIRE(env.stage() == EnvelopeStage::Sustain);
    // The decay is the drawing itself, wiggles included.
    REQUIRE(decay[decay.size() / 4] == Catch::Approx(curve.sample(1.0 / 3.0 + 1.0 / 12.0)).margin(0.01));
    REQUIRE(decay.back() == Catch::Approx(sustain).margin(0.01));
    REQUIRE(largestStep(decay, attack.back()) < 0.01f);

    run(env, 0.2);
    REQUIRE(env.level() == Catch::Approx(sustain).margin(0.001)); // sustains at the decay's end

    // Released from sustain: the release is exactly the drawing.
    env.gateOff();
    auto release = run(env, 0.11);
    REQUIRE(release[release.size() / 2] == Catch::Approx(curve.sample(2.0 / 3.0 + 0.55 / 3.0)).margin(0.02));
    REQUIRE(env.stage() == EnvelopeStage::Idle);
    REQUIRE(largestStep(release, sustain) < 0.01f);
}

TEST_CASE("An early drawn release keeps the release shape, scaled to the current level")
{
    // Attack to 1, flat decay at 0.8, release falls linearly to 0.
    EnvelopeCurve curve = drawnCurve([](double x) {
        if (x < 1.0 / 3.0)
            return float(3.0 * x);
        if (x < 2.0 / 3.0)
            return 0.8f;
        return float(0.8 * (1.0 - (x - 2.0 / 3.0) * 3.0));
    });
    DrawnTiming t;
    t.timespan = 0.3;

    Envelope env(kRate);
    env.setMode(EnvelopeMode::Drawn);
    env.setDrawn(&curve, t);
    env.gateOn();
    auto attack = run(env, 0.05); // halfway up the attack: ~0.5
    const float at = attack.back();
    env.gateOff();
    auto release = run(env, 0.1);

    // Halfway through, the drawn release is at half its start: so is the scaled one.
    REQUIRE(release[release.size() / 2] == Catch::Approx(at * 0.5f).margin(0.02));
    // Never rises above where it was released.
    for (float v : release)
        REQUIRE(v <= at + 0.001f);
    REQUIRE(largestStep(release, at) < 0.01f);
}

TEST_CASE("A drawn release that ends above zero still fades out")
{
    EnvelopeCurve curve = drawnCurve([](double) { return 0.5f; }); // flat, never reaches 0
    DrawnTiming t;
    t.timespan = 0.03;

    Envelope env(kRate);
    env.setMode(EnvelopeMode::Drawn);
    env.setDrawn(&curve, t);
    env.gateOn();
    run(env, 0.05);
    env.gateOff();
    auto release = run(env, 0.02); // 10 ms release + 5 ms tail
    REQUIRE(env.stage() == EnvelopeStage::Idle);
    REQUIRE(release.back() == 0.0f);
    REQUIRE(largestStep(release, 0.5f) < 0.01f);
}

TEST_CASE("Rendering an ADSR into a drawing matches it")
{
    AdsrParams p;
    p.attack = 0.05;
    p.decay = 0.15;
    p.sustain = 0.4;
    p.release = 0.3;
    EnvelopeCurve curve;
    DrawnTiming t;
    renderAdsrToDrawing(p, curve, t);

    REQUIRE(t.timespan == Catch::Approx(0.5));
    REQUIRE(t.attackSeconds() == Catch::Approx(0.05));
    REQUIRE(t.decaySeconds() == Catch::Approx(0.15));
    REQUIRE(curve.sample(t.split2) == Catch::Approx(0.4f).margin(0.02)); // sustain = end of decay
    REQUIRE(curve.points.front() == 0.0f);
    REQUIRE(curve.sample(t.split1) == Catch::Approx(1.0f).margin(0.02));
    REQUIRE(curve.sample(t.split2) == Catch::Approx(0.4f).margin(0.02));
    REQUIRE(curve.points.back() == Catch::Approx(0.0f).margin(0.001));
}

TEST_CASE("Engine notes follow the gate through both envelopes")
{
    AudioEngine engine;
    std::vector<float> wave(512);
    for (int i = 0; i < 512; ++i)
        wave[i] = 2.0f * i / 511.0f - 1.0f;
    engine.setWavetable(buildWavetable(wave));
    engine.setVolume(1.0f);
    engine.setAdsr(AudioEngine::kAmpEnvelope, {0.01, 0.05, 0.5, 0.05});
    engine.setAdsr(AudioEngine::kFilterEnvelope, {0.001, 0.1, 0.0, 0.1});
    engine.setFilterCurve(std::make_unique<FilterCurve>(FilterCurve::preset(FilterPreset::LowPass)));
    engine.setFilterCutoff(-3.0f);
    engine.setFilterEnvAmount(5.0f);

    auto peakOf = [&](int blocks) {
        std::vector<float> out(512 * 2);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b) {
            engine.render(out.data(), 512, 2);
            for (float s : out) {
                REQUIRE(std::isfinite(s));
                peak = std::max(peak, std::fabs(s));
            }
        }
        return peak;
    };

    REQUIRE(peakOf(4) == 0.0f); // no gate: silence
    engine.setGate(true);
    REQUIRE(peakOf(20) > 0.05f); // note sounds
    REQUIRE(engine.envelopeStatus(AudioEngine::kAmpEnvelope).stage == EnvelopeStage::Sustain);
    engine.setGate(false);
    peakOf(20); // 200 ms: well past the 50 ms release
    REQUIRE(engine.envelopeStatus(AudioEngine::kAmpEnvelope).stage == EnvelopeStage::Idle);
    REQUIRE(peakOf(4) == 0.0f);
}
