#pragma once

#include "AudioEngine.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <vector>

// The bridge between the QML user interface and the audio engine.
//
// QML sets properties (frequency, volume, holding) and hands over drawn
// shapes through setShape(). This class turns those into engine calls and
// keeps all synth logic out of the UI layer.
class SynthController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool holding READ holding WRITE setHolding NOTIFY holdingChanged)
    Q_PROPERTY(bool audioRunning READ audioRunning CONSTANT)
    Q_PROPERTY(QString statusText READ statusText CONSTANT)

public:
    explicit SynthController(QObject* parent = nullptr);

    qreal frequency() const { return frequency_; }
    void setFrequency(qreal hz);
    qreal volume() const { return volume_; }
    void setVolume(qreal volume);
    bool holding() const { return holding_; }
    void setHolding(bool on);

    bool audioRunning() const { return engine_.isRunning(); }
    QString statusText() const { return statusText_; }

    // New drawing (y-values in -1..1 across one cycle). Calls arriving in
    // quick succession while the user drags are merged, so the wavetable is
    // rebuilt at most about once per frame.
    Q_INVOKABLE void setShape(const QList<qreal>& points);

signals:
    void frequencyChanged();
    void volumeChanged();
    void holdingChanged();

private:
    void rebuildWavetable();

    hw::AudioEngine engine_;
    QTimer rebuildTimer_;
    QTimer garbageTimer_;
    std::vector<float> pendingShape_;

    qreal frequency_ = 110.0;
    qreal volume_ = 0.5;
    bool holding_ = false;
    QString statusText_;
};
