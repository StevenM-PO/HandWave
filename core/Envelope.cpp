#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace hw {

namespace {

// Shortest any stage may be: an instant jump would click.
constexpr double kMinStageSeconds = 0.001;
// Fade to silence after a drawn release that ends above zero.
constexpr double kTailSeconds = 0.005;
// Sustain level changes glide over about this long.
constexpr double kSustainGlideSeconds = 0.005;

} // namespace

float EnvelopeCurve::sample(double x) const
{
    const double pos = std::clamp(x, 0.0, 1.0) * (points.size() - 1);
    const size_t i = std::min(static_cast<size_t>(pos), points.size() - 2);
    const double frac = pos - static_cast<double>(i);
    return static_cast<float>(points[i] + (points[i + 1] - points[i]) * frac);
}

double segmentCurve(double u, double curvature)
{
    u = std::clamp(u, 0.0, 1.0);
    if (curvature < 1e-3)
        return u;
    return (1.0 - std::exp(-curvature * u)) / (1.0 - std::exp(-curvature));
}

double adsrLevel(const AdsrParams& p, EnvelopeStage stage, double seconds, double startLevel)
{
    switch (stage) {
    case EnvelopeStage::Attack:
        return startLevel + (1.0 - startLevel)
            * segmentCurve(seconds / std::max(p.attack, kMinStageSeconds), kAttackCurvature);
    case EnvelopeStage::Decay:
        return startLevel + (p.sustain - startLevel)
            * segmentCurve(seconds / std::max(p.decay, kMinStageSeconds), kDecayCurvature);
    case EnvelopeStage::Sustain:
        return p.sustain;
    case EnvelopeStage::Release:
        return startLevel * (1.0 - segmentCurve(seconds / std::max(p.release, kMinStageSeconds), kDecayCurvature));
    case EnvelopeStage::Idle:
        break;
    }
    return 0.0;
}

void renderAdsrToDrawing(const AdsrParams& adsr, EnvelopeCurve& curve, DrawnTiming& timing)
{
    const double a = std::max(adsr.attack, kMinStageSeconds);
    const double d = std::max(adsr.decay, kMinStageSeconds);
    const double r = std::max(adsr.release, kMinStageSeconds);
    const double total = a + d + r;
    const int n = static_cast<int>(curve.points.size());
    for (int i = 0; i < n; ++i) {
        const double t = total * i / (n - 1);
        double level;
        if (t < a)
            level = adsrLevel(adsr, EnvelopeStage::Attack, t, 0.0);
        else if (t < a + d)
            level = adsrLevel(adsr, EnvelopeStage::Decay, t - a, 1.0);
        else
            level = adsrLevel(adsr, EnvelopeStage::Release, t - a - d, adsr.sustain);
        curve.points[i] = static_cast<float>(level);
    }
    timing.split1 = a / total;
    timing.split2 = (a + d) / total;
    timing.timespan = total;
}

// ---- Generator ---------------------------------------------------------------

Envelope::Envelope(double sampleRate)
    : sampleRate_(sampleRate)
    , dt_(1.0 / sampleRate)
{
}

void Envelope::setSampleRate(double sampleRate)
{
    sampleRate_ = sampleRate;
    dt_ = 1.0 / sampleRate;
}

void Envelope::enter(EnvelopeStage stage)
{
    stage_ = stage;
    time_ = 0.0;
    startLevel_ = level_;
    tailLevel_ = -1.0f; // release tail not started
}

void Envelope::gateOn()
{
    enter(EnvelopeStage::Attack); // from the current level: retrigger is legato
}

void Envelope::gateOff()
{
    if (stage_ != EnvelopeStage::Idle && stage_ != EnvelopeStage::Release)
        enter(EnvelopeStage::Release);
}

double Envelope::stageDuration() const
{
    double seconds = 0.0;
    const bool drawn = mode_ == EnvelopeMode::Drawn;
    switch (stage_) {
    case EnvelopeStage::Attack: seconds = drawn ? timing_.attackSeconds() : adsr_.attack; break;
    case EnvelopeStage::Decay: seconds = drawn ? timing_.decaySeconds() : adsr_.decay; break;
    case EnvelopeStage::Release: seconds = drawn ? timing_.releaseSeconds() : adsr_.release; break;
    default: break;
    }
    return std::max(seconds, kMinStageSeconds);
}

double Envelope::drawnLevel(double u) const
{
    if (curve_ == nullptr)
        return 0.0;
    const EnvelopeCurve& c = *curve_;
    const double s1 = timing_.split1, s2 = timing_.split2;
    switch (stage_) {
    case EnvelopeStage::Attack: // ramp from where we are onto the drawing
        return c.sample(u * s1) + (startLevel_ - c.sample(0.0)) * (1.0 - u);
    case EnvelopeStage::Decay:
        return c.sample(s1 + u * (s2 - s1));
    case EnvelopeStage::Release: {
        const double shape = c.sample(s2 + u * (1.0 - s2));
        const double drawnStart = c.sample(s2); // = sustain
        // From sustain these match and the drawing is followed exactly.
        // Otherwise scale the drawn release to start from the current level;
        // if the drawing starts near 0 there's nothing to scale, so ramp instead.
        if (drawnStart > 0.02)
            return shape * (startLevel_ / drawnStart);
        return shape + (startLevel_ - drawnStart) * (1.0 - u);
    }
    default:
        return 0.0;
    }
}

float Envelope::next()
{
    // A loop only so a finished stage can hand straight over to the next one
    // within the same sample.
    for (int guard = 0; guard < 4; ++guard) {
        if (stage_ == EnvelopeStage::Idle) {
            level_ = 0.0f;
            return level_;
        }

        if (stage_ == EnvelopeStage::Sustain) {
            const double target = mode_ == EnvelopeMode::Adsr ? adsr_.sustain
                                  : curve_ ? curve_->sample(timing_.split2) : 0.0;
            level_ += static_cast<float>((target - level_) * (1.0 - std::exp(-dt_ / kSustainGlideSeconds)));
            time_ += dt_;
            return level_;
        }

        const double duration = stageDuration();
        if (time_ < duration) {
            const double v = mode_ == EnvelopeMode::Drawn ? drawnLevel(time_ / duration)
                                                          : adsrLevel(adsr_, stage_, time_, startLevel_);
            level_ = static_cast<float>(std::clamp(v, 0.0, 1.0));
            time_ += dt_;
            return level_;
        }

        // The stage has run its course.
        if (stage_ == EnvelopeStage::Attack) {
            enter(EnvelopeStage::Decay);
            continue;
        }
        if (stage_ == EnvelopeStage::Decay) {
            enter(EnvelopeStage::Sustain);
            continue;
        }
        // Release: fade whatever is left to silence, then stop.
        if (tailLevel_ < 0.0f)
            tailLevel_ = level_;
        const double tail = (time_ - duration) / kTailSeconds;
        if (tailLevel_ <= 1e-4f || tail >= 1.0) {
            enter(EnvelopeStage::Idle);
            continue;
        }
        level_ = static_cast<float>(tailLevel_ * (1.0 - tail));
        time_ += dt_;
        return level_;
    }
    return level_;
}

} // namespace hw
