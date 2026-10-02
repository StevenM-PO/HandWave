#include "SynthController.h"

#include "WavetableBuilder.h"

#include <QtGlobal>

SynthController::SynthController(QObject* parent)
    : QObject(parent)
{
    engine_.setFrequency(static_cast<float>(frequency_));
    engine_.setVolume(static_cast<float>(volume_));

    if (!engine_.start())
        statusText_ = QString::fromStdString(engine_.lastError());

    // Merge rapid drawing updates. Building a table takes about a millisecond,
    // so doing it once per ~16 ms frame is plenty and keeps the UI smooth.
    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(16);
    connect(&rebuildTimer_, &QTimer::timeout, this, &SynthController::rebuildWavetable);

    // Free the tables the audio thread has finished with.
    garbageTimer_.setInterval(250);
    connect(&garbageTimer_, &QTimer::timeout, this, [this] { engine_.collectGarbage(); });
    garbageTimer_.start();
}

void SynthController::setFrequency(qreal hz)
{
    hz = qBound<qreal>(10.0, hz, 8000.0);
    if (qFuzzyCompare(hz, frequency_))
        return;
    frequency_ = hz;
    engine_.setFrequency(static_cast<float>(hz));
    emit frequencyChanged();
}

void SynthController::setVolume(qreal volume)
{
    volume = qBound<qreal>(0.0, volume, 1.0);
    if (qFuzzyCompare(volume, volume_))
        return;
    volume_ = volume;
    engine_.setVolume(static_cast<float>(volume));
    emit volumeChanged();
}

void SynthController::setHolding(bool on)
{
    if (on == holding_)
        return;
    holding_ = on;
    engine_.setGate(on);
    emit holdingChanged();
}

void SynthController::setShape(const QList<qreal>& points)
{
    pendingShape_.assign(points.begin(), points.end());
    if (!rebuildTimer_.isActive())
        rebuildTimer_.start();
}

void SynthController::rebuildWavetable()
{
    engine_.setWavetable(hw::buildWavetable(pendingShape_));
}
