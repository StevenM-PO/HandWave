#include "ShapeEdits.h"

#include <algorithm>
#include <cmath>

namespace hw {

void rotatePhase(std::vector<float>& points, int steps)
{
    const int n = static_cast<int>(points.size());
    if (n < 3)
        return;
    const int unique = n - 1; // points.back() repeats the seam phase
    const int shift = ((steps % unique) + unique) % unique;
    if (shift == 0)
        return;

    // Right-rotate the unique points: new[i] = old[i - shift].
    std::rotate(points.begin(), points.begin() + (unique - shift), points.begin() + unique);
    // The new seam lands on a continuous part of the old drawing, so the
    // end value equals the start value.
    points.back() = points.front();
}

void joinEnds(std::vector<float>& points, float blendFraction)
{
    const int n = static_cast<int>(points.size());
    if (n < 3)
        return;
    const int last = n - 1;

    const float target = 0.5f * (points.front() + points.back());
    const float startShift = target - points.front();
    const float endShift = target - points.back();

    // Points on each side of the seam that get shifted.
    const int half = std::clamp(static_cast<int>(std::lround(blendFraction * last * 0.5f)), 1, last / 2);

    for (int d = 0; d < half; ++d) {
        // Weight 1 at the seam, easing smoothly (smoothstep) to 0 at the window edge.
        const float x = 1.0f - static_cast<float>(d) / half;
        const float w = x * x * (3.0f - 2.0f * x);
        points[d] += startShift * w;
        points[last - d] += endShift * w;
    }
}

std::vector<int> drawStrokeSegment(std::vector<float>& points, int from, float fromValue,
                                   int to, float toValue, bool periodic)
{
    std::vector<int> written;
    const int n = static_cast<int>(points.size());
    if (n < 2)
        return written;

    // Index distance to travel. On a periodic curve the last point repeats the
    // first, so one lap is n - 1 steps; going over half a lap means the other
    // way round (across the seam) is shorter.
    const int lap = n - 1;
    int delta = to - from;
    if (periodic) {
        if (delta > lap / 2)
            delta -= lap;
        else if (delta < -lap / 2)
            delta += lap;
    }

    const int span = std::abs(delta);
    const int step = delta >= 0 ? 1 : -1;
    for (int k = (span == 0 ? 0 : 1); k <= span; ++k) {
        const float t = span == 0 ? 1.0f : static_cast<float>(k) / span;
        const float value = fromValue + (toValue - fromValue) * t;
        int index = from + k * step;
        if (periodic)
            index = ((index % lap) + lap) % lap;
        points[index] = value;
        written.push_back(index);
        if (periodic && index == 0) { // the seam: first and last points coincide
            points[lap] = value;
            written.push_back(lap);
        }
    }
    return written;
}

} // namespace hw
