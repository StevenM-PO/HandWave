#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <array>
#include <vector>

class QKeyEvent;

Q_DECLARE_LOGGING_CATEGORY(lcInput)

// The instrument's physical controls as abstract events: numbered encoders
// that turn and press, and numbered switches.
//
// QML decides what each control does on the current page. Input *sources*
// only report what the hardware did, so the mapping never depends on where
// the input came from:
//   - now:   the keyboard. Hold a digit key 1-9 while turning or pressing a
//            volume knob (or [ ] Enter) to address that encoder; with no
//            digit held, the knob is encoder 1.
//   - later: GPIO encoders/switches via the kernel's rotary-encoder and
//            gpio-key drivers, calling turnEncoder()/pressEncoder()/pressSwitch().
// Keyboard switches: Space = 1, J = 2, Tab = 3.
//
// Enable `handwave.input.debug` logging to see every raw key and mapped event.
class ControlSurface : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    static constexpr int kEncoderCount = 9;

    explicit ControlSurface(QObject* parent = nullptr);
    ~ControlSurface() override;

    // ---- Entry points for input sources ----
    void turnEncoder(int encoder, int detents);
    void pressEncoder(int encoder);
    void pressSwitch(int sw);

signals:
    // `steps` is the detent count after acceleration (fast turns count extra).
    void encoderTurned(int encoder, int steps);
    void encoderPressed(int encoder);
    void switchPressed(int sw);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleKey(const QKeyEvent* event, bool pressed);
    int activeEncoder() const;

    // Digit keys currently held, oldest first; the newest selects the encoder.
    std::vector<int> heldDigits_;

    // Per-encoder timing for acceleration (index 0 unused).
    struct Motion {
        QElapsedTimer lastDetent;
        int direction = 0;
    };
    std::array<Motion, kEncoderCount + 1> motion_;
};
