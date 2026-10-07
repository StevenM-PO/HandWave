#include "Fft.h"
#include "ShapeEdits.h"
#include "WavetableBuilder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

using namespace hw;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kCount = 512;

std::vector<float> sine()
{
    std::vector<float> p(kCount);
    for (int i = 0; i < kCount; ++i)
        p[i] = float(std::sin(2 * kPi * i / (kCount - 1)));
    return p;
}

std::vector<float> saw()
{
    std::vector<float> p(kCount);
    for (int i = 0; i < kCount; ++i)
        p[i] = float(2.0 * i / (kCount - 1) - 1.0);
    return p;
}

float largestStep(const std::vector<float>& p)
{
    float largest = 0.0f;
    for (size_t i = 1; i < p.size(); ++i)
        largest = std::max(largest, std::fabs(p[i] - p[i - 1]));
    // Include the wrap from the end of the cycle back to its start.
    return std::max(largest, std::fabs(p.front() - p.back()));
}

} // namespace

TEST_CASE("rotatePhase shifts the drawing and wraps at the seam")
{
    auto p = sine();
    const auto original = p;

    rotatePhase(p, 10);
    REQUIRE(p[10] == original[0]);
    REQUIRE(p[0] == original[kCount - 1 - 10]);
    REQUIRE(p.back() == p.front());

    rotatePhase(p, -10);
    for (int i = 0; i < kCount - 1; ++i)
        REQUIRE(p[i] == original[i]);

    // A whole cycle (511 unique points) is no change.
    rotatePhase(p, kCount - 1);
    for (int i = 0; i < kCount - 1; ++i)
        REQUIRE(p[i] == original[i]);
}

TEST_CASE("Rotating a saw moves its seam jump inside the drawing")
{
    auto p = saw();
    rotatePhase(p, 100);
    REQUIRE(p.back() == p.front()); // seam is now continuous
    // The big jump now sits where the old seam moved to.
    REQUIRE(std::fabs(p[100] - p[99]) > 1.9f);
}

TEST_CASE("Rotation doesn't change the harmonic content (it only shifts phase)")
{
    auto magnitudes = [](const std::vector<float>& points) {
        auto table = buildWavetable(points);
        std::vector<std::complex<double>> data(table->levels[0].begin(), table->levels[0].end());
        fft(data, false);
        std::vector<double> m(64);
        for (int h = 0; h < 64; ++h)
            m[h] = std::abs(data[h]) / kTableSize;
        return m;
    };

    auto p = saw();
    p.back() = p.front(); // match what rotation does to the end point
    auto rotated = p;
    rotatePhase(rotated, 128); // a quarter of the cycle

    const auto a = magnitudes(p);
    const auto b = magnitudes(rotated);
    for (int h = 1; h < 64; ++h)
        REQUIRE(b[h] == Catch::Approx(a[h]).margin(1e-3));
}

TEST_CASE("joinEnds closes the seam of a saw locally")
{
    auto p = saw();
    const auto original = p;
    joinEnds(p, 0.10f); // ~25 points each side

    REQUIRE(p.front() == Catch::Approx(p.back()));
    REQUIRE(p.front() == Catch::Approx(0.0f).margin(1e-6));

    // Outside the blend window the drawing is untouched.
    for (int i = 30; i < kCount - 30; ++i)
        REQUIRE(p[i] == original[i]);

    // The 2.0 jump is spread out into a steep but continuous slope.
    REQUIRE(largestStep(p) < 0.2f);
}

TEST_CASE("joinEnds leaves an already-continuous shape alone")
{
    auto p = sine();
    const auto original = p;
    joinEnds(p, 0.10f);
    for (int i = 0; i < kCount; ++i)
        REQUIRE(p[i] == Catch::Approx(original[i]).margin(1e-6));
}

TEST_CASE("Edits cope with degenerate input")
{
    std::vector<float> tiny{0.5f, -0.5f};
    rotatePhase(tiny, 3);
    joinEnds(tiny, 0.5f);
    REQUIRE(tiny == std::vector<float>{0.5f, -0.5f});

    auto p = saw();
    joinEnds(p, 5.0f); // absurd fraction is clamped to half the cycle per side
    REQUIRE(p.front() == Catch::Approx(p.back()));
}

TEST_CASE("Stroke segments fill every point they pass")
{
    std::vector<float> p(kCount, -1.0f);

    // Non-periodic: a fast swipe over more than half the curve is filled
    // straight across (it used to be mistaken for a wrap and left a gap).
    auto written = drawStrokeSegment(p, 10, 0.0f, 400, 1.0f, false);
    REQUIRE(written.size() == 390);
    for (int i = 11; i <= 400; ++i)
        REQUIRE(p[i] == Catch::Approx((i - 10) / 390.0f));
    REQUIRE(p[5] == -1.0f); // untouched outside the segment

    // A single point (start of a stroke) writes just that point.
    written = drawStrokeSegment(p, 3, 0.5f, 3, 0.5f, false);
    REQUIRE(written == std::vector<int>{3});
    REQUIRE(p[3] == 0.5f);
}

TEST_CASE("On a looped curve, strokes cross the seam the short way round")
{
    std::vector<float> p(kCount, -1.0f); // e.g. the -48 dB tail of an old low-pass
    const int last = kCount - 1;

    // From just before the end, across the seam, to just after the start.
    drawStrokeSegment(p, last - 4, 0.2f, 4, 0.2f, true);
    for (int i : {last - 3, last - 2, last - 1, last, 0, 1, 2, 3, 4})
        REQUIRE(p[i] == Catch::Approx(0.2f)); // no stale points left at the seam
    REQUIRE(p[last] == p[0]);
    REQUIRE(p[100] == -1.0f); // didn't go the long way round

    // And backwards.
    std::vector<float> q(kCount, -1.0f);
    drawStrokeSegment(q, 3, 0.7f, last - 3, 0.7f, true);
    for (int i : {2, 1, 0, last, last - 1, last - 2, last - 3})
        REQUIRE(q[i] == Catch::Approx(0.7f));
    REQUIRE(q[200] == -1.0f);
}
