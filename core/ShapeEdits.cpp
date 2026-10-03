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

} // namespace hw
