#include "Fft.h"

#include <cassert>
#include <cmath>
#include <utility>

namespace hw {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void fft(std::vector<std::complex<double>>& data, bool inverse)
{
    const size_t n = data.size();
    assert(n > 0 && (n & (n - 1)) == 0 && "FFT size must be a power of two");

    // Reorder the input into bit-reversed index order.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // Butterfly passes, doubling the transform length each time.
    const double sign = inverse ? 1.0 : -1.0;
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = sign * 2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t start = 0; start < n; start += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> even = data[start + k];
                const std::complex<double> odd = data[start + k + len / 2] * w;
                data[start + k] = even + odd;
                data[start + k + len / 2] = even - odd;
                w *= step;
            }
        }
    }
}

} // namespace hw
