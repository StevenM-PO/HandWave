#include "CurveCanvas.h"

#include "ShapeEdits.h"

#include <QMouseEvent>
#include <QTouchEvent>

#include <algorithm>
#include <cmath>

namespace {

// If a second finger lands within this time of the first, the touch was meant
// as a two-finger scroll, so the first finger's drawing is undone.
constexpr qint64 kSecondFingerGraceMs = 200;

} // namespace

CurveCanvas::CurveCanvas(int pointCount, float minValue, float maxValue, QQuickItem* parent)
    : QQuickItem(parent)
    , points_(pointCount, 0.0f)
    , minValue_(minValue)
    , maxValue_(maxValue)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    // Handle touch directly (rather than as synthesized mouse events) so we
    // can tell one finger from two.
    setAcceptTouchEvents(true);
}

QList<qreal> CurveCanvas::samples() const
{
    return QList<qreal>(points_.begin(), points_.end());
}

void CurveCanvas::setLineColor(const QColor& color)
{
    if (color == lineColor_)
        return;
    lineColor_ = color;
    markDirty();
    emit appearanceChanged();
}

void CurveCanvas::setGridColor(const QColor& color)
{
    if (color == gridColor_)
        return;
    gridColor_ = color;
    markDirty();
    emit appearanceChanged();
}

void CurveCanvas::setLineWidth(qreal width)
{
    if (qFuzzyCompare(width, lineWidth_))
        return;
    lineWidth_ = width;
    markDirty();
    emit appearanceChanged();
}

void CurveCanvas::shapeEdited()
{
    curveDirty_ = true;
    update();
    emit samplesChanged();
}

void CurveCanvas::markDirty()
{
    curveDirty_ = gridDirty_ = true;
    update();
}

qreal CurveCanvas::valueToY(float value, const QRectF& area) const
{
    return area.top() + (maxValue_ - value) / (maxValue_ - minValue_) * area.height();
}

float CurveCanvas::yToValue(qreal y, const QRectF& area) const
{
    const float v = maxValue_ - static_cast<float>((y - area.top()) / area.height()) * (maxValue_ - minValue_);
    return std::clamp(v, minValue_, maxValue_);
}

void CurveCanvas::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        markDirty();
}

// ---- Input -------------------------------------------------------------------

void CurveCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
        beginScroll(event->position().x());
    else
        beginDraw(event->position());
    event->accept();
}

void CurveCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (gesture_ == Gesture::Scroll)
        scrollTo(event->position().x());
    else
        moveTo(event->position());
    event->accept();
}

void CurveCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    moveTo(event->position());
    endGesture();
    event->accept();
}

void CurveCanvas::mouseUngrabEvent()
{
    endGesture();
}

void CurveCanvas::touchEvent(QTouchEvent* event)
{
    if (event->type() == QEvent::TouchCancel) {
        endGesture();
        event->accept();
        return;
    }

    // Fingers still on the screen, and their average x position.
    int active = 0;
    qreal sumX = 0.0;
    QPointF single;
    for (const QEventPoint& point : event->points()) {
        if (point.state() == QEventPoint::Released)
            continue;
        ++active;
        sumX += point.position().x();
        single = point.position();
    }

    if (active >= 2) {
        if (gesture_ != Gesture::Scroll)
            beginScroll(sumX / active);
        else
            scrollTo(sumX / active);
    } else if (active == 1 && gesture_ != Gesture::Scroll) {
        // Once scrolling, stay in scroll mode until every finger lifts, so
        // lifting one finger first doesn't draw a stray line.
        if (gesture_ == Gesture::None)
            beginDraw(single);
        else
            moveTo(single);
    }

    if (event->type() == QEvent::TouchEnd)
        endGesture();
    event->accept();
}

void CurveCanvas::touchUngrabEvent()
{
    endGesture();
}

void CurveCanvas::beginDraw(const QPointF& position)
{
    if (beginCustomDrag(position)) {
        gesture_ = Gesture::Custom;
        return;
    }
    gesture_ = Gesture::Draw;
    strokeSnapshot_ = points_;
    strokeTimer_.start();
    lastIndex_ = -1;
    drawTo(position);
}

void CurveCanvas::moveTo(const QPointF& position)
{
    if (gesture_ == Gesture::Draw)
        drawTo(position);
    else if (gesture_ == Gesture::Custom)
        customDragTo(position);
}

void CurveCanvas::drawTo(const QPointF& position)
{
    const QRectF area = plotArea();
    if (area.width() <= 0 || area.height() <= 0)
        return;

    const double plotX = std::clamp((position.x() - area.left()) / area.width(), 0.0, 1.0);
    const std::optional<double> curveX = curvePositionAt(plotX);
    if (!curveX) {
        lastIndex_ = -1; // off the curve: the stroke resumes fresh when it comes back
        return;
    }

    const int last = int(points_.size()) - 1;
    const int index = std::clamp(int(std::lround(*curveX * last)), 0, last);
    const float value = yToValue(position.y(), area);

    // A fast stroke skips columns between two events: fill them with a
    // straight line (across the seam on a looped curve).
    const bool startOfStroke = lastIndex_ < 0;
    const std::vector<int> written =
        hw::drawStrokeSegment(points_, startOfStroke ? index : lastIndex_, startOfStroke ? value : lastValue_,
                              index, value, isPeriodic());
    lastIndex_ = index;
    lastValue_ = value;
    for (int i : written)
        strokeEdited(i, i);
    shapeEdited();
}

void CurveCanvas::beginScroll(qreal x)
{
    if (gesture_ == Gesture::Draw && strokeTimer_.isValid()
        && strokeTimer_.elapsed() < kSecondFingerGraceMs) {
        points_ = strokeSnapshot_;
        shapeEdited();
    }
    if (gesture_ == Gesture::Custom)
        endCustomDrag();
    gesture_ = Gesture::Scroll;
    lastScrollX_ = x;
}

void CurveCanvas::scrollTo(qreal x)
{
    const qreal dx = x - lastScrollX_;
    lastScrollX_ = x;
    if (dx != 0.0)
        scrollByPixels(dx);
}

void CurveCanvas::endGesture()
{
    if (gesture_ == Gesture::Custom)
        endCustomDrag();
    gesture_ = Gesture::None;
    strokeTimer_.invalidate();
}
