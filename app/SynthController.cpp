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

    ampEnvelope_ = new EnvelopeController(engine_, hw::AudioEngine::kAmpEnvelope, {0.005, 0.3, 0.8, 0.3}, this);
    filterEnvelope_ = new EnvelopeController(engine_, hw::AudioEngine::kFilterEnvelope, {0.005, 0.4, 0.3, 0.4}, this);

    // Envelope playheads, about 30 times a second.
    statusTimer_.setInterval(33);
    connect(&statusTimer_, &QTimer::timeout, this, [this] {
        ampEnvelope_->pollStatus();
        filterEnvelope_->pollStatus();
    });
    statusTimer_.start();

    // Merge rapid drawing updates. Building a table takes about a millisecond,
    // so doing it once per ~16 ms frame is plenty and keeps the UI smooth.
    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(16);
    connect(&rebuildTimer_, &QTimer::timeout, this, &SynthController::rebuildWavetable);

    filterCurveTimer_.setSingleShot(true);
    filterCurveTimer_.setInterval(16);
    connect(&filterCurveTimer_, &QTimer::timeout, this, &SynthController::sendFilterCurve);

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

void SynthController::setPlaying(bool on)
{
    if (on == playing_)
        return;
    playing_ = on;
    updateGate();
    emit playingChanged();
}

void SynthController::setHolding(bool on)
{
    if (on == holding_)
        return;
    holding_ = on;
    updateGate();
    emit holdingChanged();
}

void SynthController::updateGate()
{
    engine_.setGate(playing_ || holding_);
}

void SynthController::setFilterEnvAmount(qreal octaves)
{
    octaves = qBound<qreal>(-6.0, octaves, 6.0);
    if (octaves == filterEnvAmount_)
        return;
    filterEnvAmount_ = octaves;
    engine_.setFilterEnvAmount(static_cast<float>(octaves));
    emit filterChanged();
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

void SynthController::setFilterCurve(const QList<qreal>& points)
{
    pendingFilterCurve_.assign(points.begin(), points.end());
    if (!filterCurveTimer_.isActive())
        filterCurveTimer_.start();
}

void SynthController::sendFilterCurve()
{
    if (pendingFilterCurve_.size() != size_t(hw::kFilterCurvePoints))
        return;
    auto curve = std::make_unique<hw::FilterCurve>();
    curve->db = pendingFilterCurve_;
    engine_.setFilterCurve(std::move(curve));
}

void SynthController::setFilterEnabled(bool on)
{
    if (on == filterEnabled_)
        return;
    filterEnabled_ = on;
    engine_.setFilterEnabled(on);
    emit filterChanged();
}

void SynthController::setFilterShift(qreal octaves)
{
    if (octaves == filterShift_)
        return;
    filterShift_ = octaves;
    engine_.setFilterCutoff(static_cast<float>(octaves));
    emit filterChanged();
}

void SynthController::setFilterLoop(bool on)
{
    if (on == filterLoop_)
        return;
    filterLoop_ = on;
    engine_.setFilterLoop(on);
    emit filterChanged();
}

void SynthController::setResonanceX(qreal x)
{
    if (x == resonanceX_)
        return;
    resonanceX_ = x;
    sendResonance();
}

void SynthController::setResonanceDb(qreal db)
{
    if (db == resonanceDb_)
        return;
    resonanceDb_ = db;
    sendResonance();
}

void SynthController::sendResonance()
{
    engine_.setResonance(static_cast<float>(resonanceX_), static_cast<float>(resonanceDb_));
    emit filterChanged();
}
