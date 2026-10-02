#pragma once

#include <cmath>

namespace hw {

// One-pole low-pass for control values (volume, pitch...).
//
// Jumping a parameter instantly causes audible clicks or "zipper" noise.
// next() instead moves a fraction of the remaining distance each sample, so
// the value glides exponentially toward the target.
class ParamSmoother {
public:
    void setTimeConstant(double seconds, double sampleRate)
    {
        coefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
    }

    void setTarget(float target) { target_ = target; }

    // Jump straight to `value` with no glide.
    void snapTo(float value) { current_ = target_ = value; }

    float next()
    {
        current_ += coefficient_ * (target_ - current_);
        return current_;
    }

    float current() const { return current_; }

private:
    float coefficient_ = 1.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace hw
