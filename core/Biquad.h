#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace hw {

// Coefficients of one second-order IIR section, normalized so a0 = 1:
//   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
// Doubles throughout: the lowest filter bands sit very close to the unit
// circle, where float precision audibly degrades the response.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs lerp(const BiquadCoeffs& from, const BiquadCoeffs& to, double t)
    {
        return {from.b0 + (to.b0 - from.b0) * t, from.b1 + (to.b1 - from.b1) * t,
                from.b2 + (to.b2 - from.b2) * t, from.a1 + (to.a1 - from.a1) * t,
                from.a2 + (to.a2 - from.a2) * t};
    }
};

// Peaking EQ ("bell") from the RBJ Audio EQ Cookbook: boosts or cuts by
// `gainDb` around `frequencyHz`, with width set by `q`. 0 dB is a pass-through.
inline BiquadCoeffs peakingEq(double frequencyHz, double q, double gainDb, double sampleRate)
{
    constexpr double kPi = 3.14159265358979323846;
    // Keep the centre safely below Nyquist so the design stays well-formed.
    const double f = std::min(frequencyHz, 0.49 * sampleRate);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha / a;
    return {(1.0 + alpha * a) / a0, (-2.0 * cosw) / a0, (1.0 - alpha * a) / a0,
            (-2.0 * cosw) / a0, (1.0 - alpha / a) / a0};
}

// Magnitude response of one section at `frequencyHz`, in dB.
inline double magnitudeDb(const BiquadCoeffs& c, double frequencyHz, double sampleRate)
{
    constexpr double kPi = 3.14159265358979323846;
    const double w = 2.0 * kPi * frequencyHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w); // z^-1
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10(std::max(std::abs(num / den), 1e-12));
}

// Running state of one section (transposed direct form II).
struct Biquad {
    double z1 = 0.0, z2 = 0.0;

    double process(const BiquadCoeffs& c, double x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0; }
};

} // namespace hw
