#include "CurveCanvas.h"

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
    if (gesture_ == Gesture::Draw)
        drawTo(event->position());
    else if (gesture_ == Gesture::Scroll)
        scrollTo(event->position().x());
    event->accept();
}

void CurveCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (gesture_ == Gesture::Draw)
        drawTo(event->position());
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
            drawTo(single);
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
    gesture_ = Gesture::Draw;
    strokeSnapshot_ = points_;
    strokeTimer_.start();
    lastIndex_ = -1;
    drawTo(position);
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

    const int count = int(points_.size());
    const int last = count - 1;
    const int index = std::clamp(int(std::lround(*curveX * last)), 0, last);
    const float value = yToValue(position.y(), area);

    // A fast stroke skips columns between two events: fill them with a
    // straight line. A jump of over half the curve can only be a wrap-around
    // (looped display), which must not be filled across.
    int first = index;
    if (lastIndex_ < 0 || lastIndex_ == index || std::abs(index - lastIndex_) > count / 2) {
        points_[index] = value;
    } else {
        const int step = index > lastIndex_ ? 1 : -1;
        const int span = std::abs(index - lastIndex_);
        for (int k = 1; k <= span; ++k) {
            const float t = static_cast<float>(k) / span;
            points_[lastIndex_ + k * step] = lastValue_ + (value - lastValue_) * t;
        }
        first = lastIndex_;
    }
    lastIndex_ = index;
    lastValue_ = value;
    strokeEdited(std::min(first, index), std::max(first, index));
    shapeEdited();
}

void CurveCanvas::beginScroll(qreal x)
{
    if (gesture_ == Gesture::Draw && strokeTimer_.isValid()
        && strokeTimer_.elapsed() < kSecondFingerGraceMs) {
        points_ = strokeSnapshot_;
        shapeEdited();
    }
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
    gesture_ = Gesture::None;
    strokeTimer_.invalidate();
}
