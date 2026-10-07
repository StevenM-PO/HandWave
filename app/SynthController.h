#pragma once

#include "AudioEngine.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <vector>

// The bridge between the QML user interface and the audio engine.
//
// QML sets properties (frequency, volume, holding, filter settings) and hands
// over drawn shapes through setShape() / setFilterCurve(). This class turns
// those into engine calls and keeps all synth logic out of the UI layer.
class SynthController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool holding READ holding WRITE setHolding NOTIFY holdingChanged)
    Q_PROPERTY(bool filterEnabled READ filterEnabled WRITE setFilterEnabled NOTIFY filterChanged)
    Q_PROPERTY(qreal filterShift READ filterShift WRITE setFilterShift NOTIFY filterChanged)
    Q_PROPERTY(bool filterLoop READ filterLoop WRITE setFilterLoop NOTIFY filterChanged)
    Q_PROPERTY(qreal resonanceX READ resonanceX WRITE setResonanceX NOTIFY filterChanged)
    Q_PROPERTY(qreal resonanceDb READ resonanceDb WRITE setResonanceDb NOTIFY filterChanged)
    Q_PROPERTY(qreal sampleRate READ sampleRate CONSTANT)
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

    bool filterEnabled() const { return filterEnabled_; }
    void setFilterEnabled(bool on);
    qreal filterShift() const { return filterShift_; }
    void setFilterShift(qreal octaves);
    bool filterLoop() const { return filterLoop_; }
    void setFilterLoop(bool on);
    qreal resonanceX() const { return resonanceX_; }
    void setResonanceX(qreal x);
    qreal resonanceDb() const { return resonanceDb_; }
    void setResonanceDb(qreal db);
    qreal sampleRate() const { return engine_.sampleRate(); }

    bool audioRunning() const { return engine_.isRunning(); }
    QString statusText() const { return statusText_; }

    // New drawing (y-values in -1..1 across one cycle). Calls arriving in
    // quick succession while the user drags are merged, so the wavetable is
    // rebuilt at most about once per frame.
    Q_INVOKABLE void setShape(const QList<qreal>& points);
    // New filter curve (dB values across the display), merged the same way.
    Q_INVOKABLE void setFilterCurve(const QList<qreal>& points);

signals:
    void frequencyChanged();
    void volumeChanged();
    void holdingChanged();
    void filterChanged();

private:
    void rebuildWavetable();
    void sendFilterCurve();
    void sendResonance();

    hw::AudioEngine engine_;
    QTimer rebuildTimer_;
    QTimer garbageTimer_;
    std::vector<float> pendingShape_;
    QTimer filterCurveTimer_;
    std::vector<float> pendingFilterCurve_;

    qreal frequency_ = 110.0;
    qreal volume_ = 0.5;
    bool holding_ = false;
    bool filterEnabled_ = true;
    qreal filterShift_ = 0.0;
    bool filterLoop_ = false;
    qreal resonanceX_ = 0.5;
    qreal resonanceDb_ = 0.0;
    QString statusText_;
};
