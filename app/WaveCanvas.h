#pragma once

#include "CurveCanvas.h"

// The oscillator's drawing surface: one waveform cycle.
//
// The shape is stored as kPointCount y-values in -1..1 evenly spaced from the
// left edge (start of the cycle) to the right edge (end of the cycle). A
// dimmed "ghost" of the next cycle's start is drawn past the right edge, so a
// jump at the seam is visible. Scrolling (two fingers / right-drag) rotates
// the phase.
//
// Edits are also exposed as Q_INVOKABLE actions (rotatePhase, joinEnds...),
// so buttons and hardware controls can drive them too.
class WaveCanvas : public CurveCanvas {
    Q_OBJECT
    QML_ELEMENT

public:
    static constexpr int kPointCount = 512;
    // Width of the ghost region, as a fraction of one cycle.
    static constexpr qreal kGhostFraction = 0.06;

    explicit WaveCanvas(QQuickItem* parent = nullptr);

    // ---- Actions ----
    // Replace the drawing with a classic shape: "sine", "triangle", "saw", "square".
    Q_INVOKABLE void loadPreset(const QString& name);
    // Flat line (silence).
    Q_INVOKABLE void clear();
    // Shift the drawing around the cycle; positive moves it right. One step is
    // one point (1/511 of the cycle).
    Q_INVOKABLE void rotatePhase(int steps);
    // Bend the ends of the drawing so they meet, over `blendFraction` of the cycle.
    Q_INVOKABLE void joinEnds(qreal blendFraction = 0.05);

protected:
    QRectF plotArea() const override;
    void scrollByPixels(qreal dx) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    qreal scrollRemainder_ = 0.0; // sub-point scroll distance carried between moves
};
