#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include <vector>

// Shared base for the surfaces you draw a curve on (the oscillator's
// waveform, the filter's response).
//
// It owns the curve (a fixed number of evenly spaced values in
// [minValue, maxValue]) and the input handling:
//   one finger / left mouse     draw (fast strokes are gap-filled)
//   two fingers / right mouse   drag sideways -> scrollByPixels()
// Subclasses decide how the plot maps to the curve and how it's rendered.
class CurveCanvas : public QQuickItem {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QList<qreal> samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY appearanceChanged)

public:
    CurveCanvas(int pointCount, float minValue, float maxValue, QQuickItem* parent = nullptr);

    QList<qreal> samples() const;
    const std::vector<float>& points() const { return points_; }

    QColor lineColor() const { return lineColor_; }
    void setLineColor(const QColor& color);
    QColor gridColor() const { return gridColor_; }
    void setGridColor(const QColor& color);
    qreal lineWidth() const { return lineWidth_; }
    void setLineWidth(qreal width);

signals:
    void samplesChanged();
    void appearanceChanged();

protected:
    // ---- For subclasses ----
    // The rectangle the curve is plotted in.
    virtual QRectF plotArea() const = 0;
    // Curve position (0..1 along the points) under a plot position (0..1
    // across the plot), or nothing if no part of the curve is shown there.
    virtual std::optional<double> curvePositionAt(double plotX) const { return plotX; }
    // True if the curve wraps around (its last point meets its first), so
    // strokes may cross the seam.
    virtual bool isPeriodic() const { return false; }
    // Points firstIndex..lastIndex were just changed by a stroke.
    virtual void strokeEdited(int /*firstIndex*/, int /*lastIndex*/) {}
    // Two-finger / right-button sideways drag.
    virtual void scrollByPixels(qreal dx) = 0;
    // A one-finger / left press can be claimed for dragging something (a
    // handle) instead of drawing: return true to get customDragTo() calls
    // and a final endCustomDrag().
    virtual bool beginCustomDrag(const QPointF& /*position*/) { return false; }
    virtual void customDragTo(const QPointF& /*position*/) {}
    virtual void endCustomDrag() {}

    // Call after changing points_: redraws and notifies.
    void shapeEdited();
    // Mark everything for redrawing (geometry or appearance changed).
    void markDirty();

    qreal valueToY(float value, const QRectF& area) const;
    float yToValue(qreal y, const QRectF& area) const;

    std::vector<float> points_;
    const float minValue_;
    const float maxValue_;
    QColor lineColor_{0x4f, 0xd1, 0xc5};
    QColor gridColor_{0x2a, 0x31, 0x3c};
    qreal lineWidth_ = 3.0;
    bool curveDirty_ = true; // the scene graph needs rebuilding
    bool gridDirty_ = true;

    // ---- Input ----
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void touchEvent(QTouchEvent* event) override;
    void touchUngrabEvent() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    enum class Gesture { None, Draw, Scroll, Custom };

    void beginDraw(const QPointF& position);
    void moveTo(const QPointF& position); // continue a draw or custom drag
    void drawTo(const QPointF& position);
    void beginScroll(qreal x);
    void scrollTo(qreal x);
    void endGesture();

    Gesture gesture_ = Gesture::None;
    int lastIndex_ = -1;
    float lastValue_ = 0.0f;
    qreal lastScrollX_ = 0.0;
    // Lets a two-finger scroll undo the dot the first finger drew a moment
    // before the second finger landed.
    std::vector<float> strokeSnapshot_;
    QElapsedTimer strokeTimer_;
};
