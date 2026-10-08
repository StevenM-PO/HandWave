#include "EnvelopeController.h"

#include <algorithm>
#include <cmath>

EnvelopeController::EnvelopeController(hw::AudioEngine& engine, int envelope, const hw::AdsrParams& defaults,
                                       QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , envelope_(envelope)
    , adsr_(defaults)
{
    sendAdsr();
    sendTiming();
    engine_.setEnvelopeMode(envelope_, hw::EnvelopeMode::Adsr);

    curveTimer_.setSingleShot(true);
    curveTimer_.setInterval(16);
    connect(&curveTimer_, &QTimer::timeout, this, [this] {
        if (pendingCurve_.size() != size_t(hw::kEnvelopePoints))
            return;
        auto curve = std::make_unique<hw::EnvelopeCurve>();
        curve->points = pendingCurve_;
        engine_.setEnvelopeCurve(envelope_, std::move(curve));
    });
}

void EnvelopeController::sendAdsr()
{
    engine_.setAdsr(envelope_, adsr_);
    emit settingsChanged();
}

void EnvelopeController::sendTiming()
{
    engine_.setDrawnTiming(envelope_, timing_);
    emit settingsChanged();
}

void EnvelopeController::setMode(int mode)
{
    mode = std::clamp(mode, 0, 1);
    if (mode == mode_)
        return;
    mode_ = mode;
    engine_.setEnvelopeMode(envelope_, static_cast<hw::EnvelopeMode>(mode));
    emit settingsChanged();
}

void EnvelopeController::setAttack(qreal seconds)
{
    seconds = std::clamp<qreal>(seconds, kMinTime, kMaxAttack);
    if (seconds == adsr_.attack)
        return;
    adsr_.attack = seconds;
    sendAdsr();
}

void EnvelopeController::setDecay(qreal seconds)
{
    seconds = std::clamp<qreal>(seconds, kMinTime, kMaxDecayRelease);
    if (seconds == adsr_.decay)
        return;
    adsr_.decay = seconds;
    sendAdsr();
}

void EnvelopeController::setSustain(qreal level)
{
    level = std::clamp<qreal>(level, 0.0, 1.0);
    if (level == adsr_.sustain)
        return;
    adsr_.sustain = level;
    sendAdsr();
}

void EnvelopeController::setRelease(qreal seconds)
{
    seconds = std::clamp<qreal>(seconds, kMinTime, kMaxDecayRelease);
    if (seconds == adsr_.release)
        return;
    adsr_.release = seconds;
    sendAdsr();
}

void EnvelopeController::setSplit1(qreal x)
{
    // Each segment keeps a minimum width so its divider stays grabbable.
    x = std::clamp<qreal>(x, kMinSegment, timing_.split2 - kMinSegment);
    if (x == timing_.split1)
        return;
    timing_.split1 = x;
    sendTiming();
}

void EnvelopeController::setSplit2(qreal x)
{
    x = std::clamp<qreal>(x, timing_.split1 + kMinSegment, 1.0 - kMinSegment);
    if (x == timing_.split2)
        return;
    timing_.split2 = x;
    sendTiming();
}

void EnvelopeController::setTimespan(qreal seconds)
{
    seconds = std::clamp<qreal>(seconds, kMinTimespan, kMaxTimespan);
    if (seconds == timing_.timespan)
        return;
    timing_.timespan = seconds;
    sendTiming();
}

void EnvelopeController::setTiming(const hw::DrawnTiming& timing)
{
    timing_ = timing;
    timing_.split1 = std::clamp(timing_.split1, kMinSegment, 1.0 - 2 * kMinSegment);
    timing_.split2 = std::clamp(timing_.split2, timing_.split1 + kMinSegment, 1.0 - kMinSegment);
    timing_.timespan = std::clamp(timing_.timespan, kMinTimespan, kMaxTimespan);
    sendTiming();
}

void EnvelopeController::setCurve(const QList<qreal>& points)
{
    pendingCurve_.assign(points.begin(), points.end());
    if (!curveTimer_.isActive())
        curveTimer_.start();
}

qreal EnvelopeController::stepTime(qreal seconds, int steps, qreal minimum, qreal maximum) const
{
    return std::clamp<qreal>(seconds * std::pow(1.06, steps), minimum, maximum);
}

void EnvelopeController::pollStatus()
{
    const auto status = engine_.envelopeStatus(envelope_);
    if (status.stage == status_.stage && status.stageSeconds == status_.stageSeconds && status.level == status_.level)
        return;
    status_ = status;
    emit statusChanged();
}
