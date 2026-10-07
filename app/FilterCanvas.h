#pragma once

#include "CurveCanvas.h"
#include "DrawnFilter.h"

#include <memory>

// The filter's drawing surface: gain (dB) against log frequency.
//
// You draw the response curve; the cutoff slides it along the frequency axis
// without editing it (two-finger / right-drag, or an encoder). Without loop,
// the curve's end values extend into the space it leaves; those stretches are
// drawn dimmed and can't be drawn on. The resonance point sits on the curve
// and moves with it. A stroke that crosses the resonance point sets its level
// to the drawn value (the drawing takes priority).
//
// A thin second line shows the filter's actual response, computed with the
// same design code the audio uses, so you see what you hear.
class FilterCanvas : public CurveCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal shiftOctaves READ shiftOctaves WRITE setShiftOctaves NOTIFY settingsChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY settingsChanged)
    Q_PROPERTY(qreal resonanceX READ resonanceX WRITE setResonanceX NOTIFY settingsChanged)
    Q_PROPERTY(qreal resonanceDb READ resonanceDb WRITE setResonanceDb NOTIFY settingsChanged)
    // Where the resonance currently is, for readouts (0 when shifted off the display).
    Q_PROPERTY(qreal resonanceHz READ resonanceHz NOTIFY settingsChanged)
    Q_PROPERTY(qreal sampleRate READ sampleRate WRITE setSampleRate NOTIFY settingsChanged)
    Q_PROPERTY(QColor responseColor READ responseColor WRITE setResponseColor NOTIFY appearanceChanged)

public:
    static constexpr double kMaxShiftOctaves = 10.0;

    explicit FilterCanvas(QQuickItem* parent = nullptr);

    qreal shiftOctaves() const { return settings_.shiftOctaves; }
    void setShiftOctaves(qreal octaves);
    bool loop() const { return settings_.loop; }
    void setLoop(bool on);
    qreal resonanceX() const { return settings_.resonanceX; }
    void setResonanceX(qreal x);
    qreal resonanceDb() const { return settings_.resonanceDb; }
    void setResonanceDb(qreal db);
    qreal resonanceHz() const;
    qreal sampleRate() const;
    void setSampleRate(qreal rate);
    QColor responseColor() const { return responseColor_; }
    void setResponseColor(const QColor& color);

    // ---- Actions ----
    // "lowpass", "highpass", "bandpass" or "flat". Moves the resonance to the
    // preset's corner/centre, with no peak.
    Q_INVOKABLE void loadPreset(const QString& name);
    // Flatten the resonance into the curve (no peak or dip).
    Q_INVOKABLE void resetResonance();
    // Move the resonance along the curve by semitones.
    Q_INVOKABLE void moveResonance(qreal semitones);

    // Display position of a frequency / gain, for placing labels from QML.
    Q_INVOKABLE qreal xForHz(qreal hz) const;
    Q_INVOKABLE qreal yForDb(qreal db) const;

signals:
    void settingsChanged();

protected:
    QRectF plotArea() const override;
    std::optional<double> curvePositionAt(double plotX) const override;
    bool isPeriodic() const override { return settings_.loop; }
    void strokeEdited(int firstIndex, int lastIndex) override;
    void scrollByPixels(qreal dx) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    void settingsEdited();
    hw::FilterCurve curve() const;

    hw::FilterSettings settings_;
    std::unique_ptr<hw::FilterDesigner> designer_;
    QColor responseColor_{0xe6, 0xed, 0xf3};
};
