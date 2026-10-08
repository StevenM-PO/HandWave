#pragma once

#include "CurveCanvas.h"
#include "EnvelopeController.h"

#include <QPointer>

// An envelope's screen: level (0..1) against time, in either mode of its
// EnvelopeController.
//
//   ADSR  - the classic shape, edited by dragging its three corner handles:
//           the attack peak (time), the decay end (time + sustain level) and
//           the release end (time). The sustain stretch is drawn at a fixed
//           width. The time axis fits the envelope, but is frozen while a
//           handle is dragged so it doesn't move under the finger.
//   Drawn - you draw the curve. Dotted dividers split it into attack, decay
//           and release (each its own colour); drag their tabs along the top
//           edge to move them. The note sustains at the drawing's level at
//           the decay/release divider.
//
// A vertical line sweeps across as a note plays, showing how far through
// the envelope it is. The first switch to
// Drawn copies the current ADSR into the drawing.
class EnvelopeCanvas : public CurveCanvas {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EnvelopeController* envelope READ envelope WRITE setEnvelope NOTIFY envelopeChanged)
    Q_PROPERTY(QColor attackColor MEMBER attackColor_ NOTIFY appearanceChanged)
    Q_PROPERTY(QColor decayColor MEMBER decayColor_ NOTIFY appearanceChanged)
    Q_PROPERTY(QColor releaseColor MEMBER releaseColor_ NOTIFY appearanceChanged)
    Q_PROPERTY(QColor handleColor MEMBER handleColor_ NOTIFY appearanceChanged)
    // Drawn mode's sustain level: the drawing at the decay/release divider.
    Q_PROPERTY(qreal sustainLevel READ sustainLevel NOTIFY sustainLevelChanged)

public:
    explicit EnvelopeCanvas(QQuickItem* parent = nullptr);

    EnvelopeController* envelope() const { return envelope_; }
    void setEnvelope(EnvelopeController* envelope);
    qreal sustainLevel() const;

signals:
    void envelopeChanged();
    void sustainLevelChanged();

protected:
    QRectF plotArea() const override;
    void scrollByPixels(qreal) override {}
    bool beginCustomDrag(const QPointF& position) override;
    void customDragTo(const QPointF& position) override;
    void endCustomDrag() override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

private:
    enum class Handle { None, Split1, Split2, AttackPeak, DecayEnd, ReleaseEnd };

    // ADSR layout: x of each stage boundary, for the current time scale.
    struct AdsrLayout {
        qreal start, attackEnd, decayEnd, sustainEnd, releaseEnd;
        qreal pixelsPerSecond;
    };
    AdsrLayout adsrLayout() const;
    bool drawnMode() const;
    void settingsChanged();

    QPointer<EnvelopeController> envelope_;
    bool seeded_ = false;     // drawing initialised from the ADSR yet?
    Handle dragging_ = Handle::None;
    qreal frozenScale_ = 0.0; // ADSR px/second held during a drag (0 = fit)

    QColor attackColor_{0x4f, 0xd1, 0xc5};
    QColor decayColor_{0xf6, 0xad, 0x55};
    QColor releaseColor_{0xb7, 0x94, 0xf4};
    QColor handleColor_{0xe6, 0xed, 0xf3};
};
