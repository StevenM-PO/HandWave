#pragma once

#include <QColor>
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
// left edge (start of the cycle) to the right edge (end of the cycle).
class WaveCanvas : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<qreal> samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY appearanceChanged)

public:
    static constexpr int kPointCount = 512;

    explicit WaveCanvas(QQuickItem* parent = nullptr);

    QList<qreal> samples() const;

    QColor lineColor() const { return lineColor_; }
    void setLineColor(const QColor& color);
    QColor gridColor() const { return gridColor_; }
    void setGridColor(const QColor& color);
    qreal lineWidth() const { return lineWidth_; }
    void setLineWidth(qreal width);

    // Replace the drawing with a classic shape: "sine", "triangle", "saw", "square".
    Q_INVOKABLE void loadPreset(const QString& name);
    // Flat line (silence).
    Q_INVOKABLE void clear();

signals:
    void samplesChanged();
    void appearanceChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    void drawTo(const QPointF& position);
    void shapeEdited();
    QRectF plotArea() const;
    qreal valueToY(float value, const QRectF& area) const;

    std::vector<float> points_;
    bool drawing_ = false;
    int lastIndex_ = -1;
    float lastValue_ = 0.0f;

    QColor lineColor_{0x4f, 0xd1, 0xc5};
    QColor gridColor_{0x2a, 0x31, 0x3c};
    qreal lineWidth_ = 3.0;

    // Which scene-graph parts need rebuilding on the next frame.
    bool waveDirty_ = true;
    bool gridDirty_ = true;
};
