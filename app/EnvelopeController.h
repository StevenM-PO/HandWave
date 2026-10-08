#pragma once

#include "AudioEngine.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <vector>

// The settings of one envelope (amp or filter), shared by its page's knobs,
// canvas and readouts, and passed on to the audio engine. Times are in
// seconds, levels 0..1. Also reports where the envelope is playing, for the
// canvas's playhead.
class EnvelopeController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SynthController")
    // 0 = ADSR, 1 = Drawn
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(qreal attack READ attack WRITE setAttack NOTIFY settingsChanged)
    Q_PROPERTY(qreal decay READ decay WRITE setDecay NOTIFY settingsChanged)
    Q_PROPERTY(qreal sustain READ sustain WRITE setSustain NOTIFY settingsChanged)
    Q_PROPERTY(qreal release READ release WRITE setRelease NOTIFY settingsChanged)
    // Drawn mode: divider positions (fractions of the width) and total time.
    // (Its sustain level is the drawing's value at split2.)
    Q_PROPERTY(qreal split1 READ split1 WRITE setSplit1 NOTIFY settingsChanged)
    Q_PROPERTY(qreal split2 READ split2 WRITE setSplit2 NOTIFY settingsChanged)
    Q_PROPERTY(qreal timespan READ timespan WRITE setTimespan NOTIFY settingsChanged)
    // Playhead (EnvelopeStage as int, seconds into it, level).
    Q_PROPERTY(int stage READ stage NOTIFY statusChanged)
    Q_PROPERTY(qreal stageSeconds READ stageSeconds NOTIFY statusChanged)
    Q_PROPERTY(qreal level READ level NOTIFY statusChanged)

public:
    // Knob ranges.
    static constexpr double kMinTime = 0.001;
    static constexpr double kMaxAttack = 10.0;
    static constexpr double kMaxDecayRelease = 20.0;
    static constexpr double kMinTimespan = 0.01;
    static constexpr double kMaxTimespan = 30.0;
    static constexpr double kMinSegment = 0.01; // smallest drawn segment, fraction of the width

    EnvelopeController(hw::AudioEngine& engine, int envelope, const hw::AdsrParams& defaults,
                       QObject* parent = nullptr);

    int mode() const { return mode_; }
    void setMode(int mode);
    qreal attack() const { return adsr_.attack; }
    void setAttack(qreal seconds);
    qreal decay() const { return adsr_.decay; }
    void setDecay(qreal seconds);
    qreal sustain() const { return adsr_.sustain; }
    void setSustain(qreal level);
    qreal release() const { return adsr_.release; }
    void setRelease(qreal seconds);
    qreal split1() const { return timing_.split1; }
    void setSplit1(qreal x);
    qreal split2() const { return timing_.split2; }
    void setSplit2(qreal x);
    qreal timespan() const { return timing_.timespan; }
    void setTimespan(qreal seconds);

    int stage() const { return int(status_.stage); }
    qreal stageSeconds() const { return status_.stageSeconds; }
    qreal level() const { return status_.level; }

    const hw::AdsrParams& adsr() const { return adsr_; }
    const hw::DrawnTiming& timing() const { return timing_; }
    void setTiming(const hw::DrawnTiming& timing);

    // New drawing (levels 0..1). Rapid updates are merged, as for wavetables.
    Q_INVOKABLE void setCurve(const QList<qreal>& points);
    // Turn a time knob by `steps` detents: about 6% per detent, so the whole
    // range from 1 ms to 20 s is about 170 detents (fewer with acceleration).
    Q_INVOKABLE qreal stepTime(qreal seconds, int steps, qreal minimum, qreal maximum) const;

    // Called regularly by SynthController to refresh the playhead.
    void pollStatus();

signals:
    void settingsChanged();
    void statusChanged();

private:
    void sendAdsr();
    void sendTiming();

    hw::AudioEngine& engine_;
    const int envelope_;
    int mode_ = 0;
    hw::AdsrParams adsr_;
    hw::DrawnTiming timing_;
    hw::AudioEngine::EnvelopeStatus status_;
    QTimer curveTimer_;
    std::vector<float> pendingCurve_;
};
