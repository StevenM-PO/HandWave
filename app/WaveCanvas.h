#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>
#include <vector>

// A surface you draw one waveform cycle on with a finger or the mouse.
//
// Rendering goes through the Qt Quick scene graph (GPU geometry nodes),
// not QML Canvas. That keeps it smooth on the Raspberry Pi and leaves room
// for animated effects (morphing, custom shaders) later.
//
// The shape is stored as kPointCount y-values in -1..1 evenly spaced from the
// left edge (start of the cycle) to the right edge (end of the cycle). A
// dimmed "ghost" of the next cycle's start is drawn past the right edge, so a
// jump at the seam is visible.
//
// Input:
//   one finger / left mouse     draw
//   two fingers / right mouse   drag sideways to scroll the phase
// Edits are also exposed as Q_INVOKABLE actions (rotatePhase, joinEnds...),
// so buttons, keys, and later the hardware encoders can drive them too.
class WaveCanvas : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<qreal> samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY appearanceChanged)

public:
    static constexpr int kPointCount = 512;
    // Width of the ghost region, as a fraction of one cycle.
    static constexpr qreal kGhostFraction = 0.06;

    explicit WaveCanvas(QQuickItem* parent = nullptr);

    QList<qreal> samples() const;

    QColor lineColor() const { return lineColor_; }
    void setLineColor(const QColor& color);
    QColor gridColor() const { return gridColor_; }
    void setGridColor(const QColor& color);
    qreal lineWidth() const { return lineWidth_; }
    void setLineWidth(qreal width);

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

signals:
    void samplesChanged();
    void appearanceChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void touchEvent(QTouchEvent* event) override;
    void touchUngrabEvent() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    enum class Gesture { None, Draw, Scroll };

    void beginDraw(const QPointF& position);
    void drawTo(const QPointF& position);
    void beginScroll(qreal x);
    void scrollTo(qreal x);
    void endGesture();

    void shapeEdited();
    QRectF plotArea() const;
    qreal valueToY(float value, const QRectF& area) const;

    std::vector<float> points_;

    Gesture gesture_ = Gesture::None;
    int lastIndex_ = -1;
    float lastValue_ = 0.0f;
    qreal scrollAnchorX_ = 0.0;
    // Lets a two-finger scroll undo the dot the first finger drew a moment
    // before the second finger landed.
    std::vector<float> strokeSnapshot_;
    QElapsedTimer strokeTimer_;

    QColor lineColor_{0x4f, 0xd1, 0xc5};
    QColor gridColor_{0x2a, 0x31, 0x3c};
    qreal lineWidth_ = 3.0;

    // Which scene-graph parts need rebuilding on the next frame.
    bool waveDirty_ = true;
    bool gridDirty_ = true;
};
