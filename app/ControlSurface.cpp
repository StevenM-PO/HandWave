#include "ControlSurface.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>

#include <algorithm>

Q_LOGGING_CATEGORY(lcInput, "handwave.input", QtWarningMsg)

namespace {

// Detents closer together than this, in the same direction, are a fast spin
// and count kFastMultiplier steps each.
constexpr qint64 kFastIntervalMs = 40;
constexpr int kFastMultiplier = 4;

} // namespace

ControlSurface::ControlSurface(QObject* parent)
    : QObject(parent)
{
    // Watch keys application-wide, so the controls work whichever item has focus.
    qApp->installEventFilter(this);
}

ControlSurface::~ControlSurface()
{
    if (qApp)
        qApp->removeEventFilter(this);
}

// ---- Abstract controls ------------------------------------------------------

void ControlSurface::turnEncoder(int encoder, int detents)
{
    if (encoder < 1 || encoder > kEncoderCount || detents == 0)
        return;

    Motion& motion = motion_[encoder];
    const int direction = detents > 0 ? 1 : -1;
    int steps = detents;
    if (motion.direction == direction && motion.lastDetent.isValid()
        && motion.lastDetent.elapsed() < kFastIntervalMs)
        steps *= kFastMultiplier;
    motion.direction = direction;
    motion.lastDetent.start();

    qCDebug(lcInput) << "encoder" << encoder << "turned" << detents << "->" << steps << "steps";
    emit encoderTurned(encoder, steps);
}

void ControlSurface::pressEncoder(int encoder)
{
    if (encoder < 1 || encoder > kEncoderCount)
        return;
    qCDebug(lcInput) << "encoder" << encoder << "pressed";
    emit encoderPressed(encoder);
}

void ControlSurface::pressSwitch(int sw)
{
    qCDebug(lcInput) << "switch" << sw << "pressed";
    emit switchPressed(sw);
}

// ---- Keyboard source --------------------------------------------------------

int ControlSurface::activeEncoder() const
{
    return heldDigits_.empty() ? 1 : heldDigits_.back();
}

bool ControlSurface::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        // A key event is delivered to the window and then forwarded to the
        // focused item; handle it once, at the window.
        if (qobject_cast<QWindow*>(watched))
            return handleKey(static_cast<QKeyEvent*>(event), event->type() == QEvent::KeyPress);
        break;
    case QEvent::ApplicationDeactivate:
        // We won't see the releases of keys held while focus leaves.
        heldDigits_.clear();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool ControlSurface::handleKey(const QKeyEvent* event, bool pressed)
{
    const int key = event->key();
    const bool repeat = event->isAutoRepeat();
    qCDebug(lcInput).nospace() << (pressed ? "press " : "release ") << Qt::hex << "key=0x" << key
                               << " scan=" << event->nativeScanCode() << Qt::dec
                               << " text=" << event->text() << (repeat ? " (repeat)" : "");

    // Digits select the encoder while held. Shifted digits (!, @...) don't count.
    if (key >= Qt::Key_1 && key <= Qt::Key_9 && !(event->modifiers() & Qt::ShiftModifier)) {
        if (repeat)
            return true;
        const int digit = key - Qt::Key_0;
        heldDigits_.erase(std::remove(heldDigits_.begin(), heldDigits_.end(), digit), heldDigits_.end());
        if (pressed)
            heldDigits_.push_back(digit);
        return true;
    }

    // Everything below acts on press. Its release is consumed so nothing else
    // reacts to half of a key we handled.
    switch (key) {
    // Turns: the knob sends volume keys (or F13/F14 if remapped in Keychron
    // Launcher); [ and ] are the knob-less fallback. Auto-repeat keeps turning.
    case Qt::Key_VolumeUp:
    case Qt::Key_F14:
    case Qt::Key_BracketRight:
        if (pressed)
            turnEncoder(activeEncoder(), 1);
        return true;
    case Qt::Key_VolumeDown:
    case Qt::Key_F13:
    case Qt::Key_BracketLeft:
        if (pressed)
            turnEncoder(activeEncoder(), -1);
        return true;

    // Presses: one per physical press, so auto-repeat is ignored.
    case Qt::Key_VolumeMute:
    case Qt::Key_F15:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (pressed && !repeat)
            pressEncoder(activeEncoder());
        return true;
    case Qt::Key_Space:
        if (pressed && !repeat)
            pressSwitch(1);
        return true;
    case Qt::Key_J:
        if (pressed && !repeat)
            pressSwitch(2);
        return true;
    case Qt::Key_Tab:
        if (pressed && !repeat)
            pressSwitch(3);
        return true;
    default:
        return false;
    }
}
