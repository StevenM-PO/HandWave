#include "WaveCanvas.h"

#include "ShapeEdits.h"

#include <QMouseEvent>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QTouchEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// Children of the root node, in back-to-front order.
enum NodeIndex { GridNode = 0, FillNode = 1, GhostNode = 2, LineNode = 3 };

// Soft edge (in pixels) added on each side of the line for anti-aliasing.
constexpr float kFeather = 1.0f;

// If a second finger lands within this time of the first, the touch was meant
// as a two-finger scroll, so the first finger's drawing is undone.
constexpr qint64 kSecondFingerGraceMs = 200;

// Colors for QSGVertexColorMaterial must be premultiplied by alpha.
void setColoredVertex(QSGGeometry::ColoredPoint2D& v, float x, float y, const QColor& c, float alpha)
{
    const float a = static_cast<float>(c.alphaF()) * alpha;
    v.set(x, y,
          static_cast<uchar>(c.red() * a), static_cast<uchar>(c.green() * a),
          static_cast<uchar>(c.blue() * a), static_cast<uchar>(255 * a));
}

QSGGeometryNode* makeNode(QSGGeometry* geometry, QSGMaterial* material)
{
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

QSGGeometry* makeRibbonGeometry()
{
    auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0,
                              QSGGeometry::UnsignedShortType);
    g->setDrawingMode(QSGGeometry::DrawTriangles);
    return g;
}

// Fill `g` with a thick, anti-aliased line through `p`.
//
// GPU line primitives can't reliably draw thick, smooth lines, so the line is
// built as a ribbon of triangles. Each point gets 4 vertices across the line
// (soft edge, solid, solid, soft edge), and neighbours are joined by 3 strips
// of quads.
void buildRibbon(QSGGeometry* g, const std::vector<QPointF>& p, float halfWidth,
                 const QColor& color, float alpha)
{
    const int count = static_cast<int>(p.size());
    if (count < 2) {
        g->allocate(0, 0);
        return;
    }
    const int vertexCount = 4 * count;
    const int indexCount = 3 * 6 * (count - 1);
    if (g->vertexCount() != vertexCount || g->indexCount() != indexCount)
        g->allocate(vertexCount, indexCount);

    auto segmentNormal = [&](int a, int b) {
        const QPointF d = p[b] - p[a];
        const qreal len = std::hypot(d.x(), d.y());
        return len > 1e-6 ? QPointF(-d.y() / len, d.x() / len) : QPointF(0, 0);
    };

    auto* v = g->vertexDataAsColoredPoint2D();
    for (int i = 0; i < count; ++i) {
        // Mitre join: average the neighbouring segment normals and lengthen
        // the offset so the line keeps its width at corners.
        const QPointF n0 = segmentNormal(std::max(i - 1, 0), std::max(i, 1));
        const QPointF n1 = segmentNormal(std::min(i, count - 2), std::min(i + 1, count - 1));
        const QPointF reference = (n1.x() != 0 || n1.y() != 0) ? n1 : n0;
        QPointF mitre = n0 + n1;
        const qreal mitreLen = std::hypot(mitre.x(), mitre.y());
        mitre = mitreLen > 1e-6 ? mitre / mitreLen : (reference.isNull() ? QPointF(0, -1) : reference);
        const qreal dot = reference.isNull() ? 1.0 : mitre.x() * reference.x() + mitre.y() * reference.y();
        const float scale = static_cast<float>(1.0 / std::max(dot, 0.35)); // cap spikes

        const float inner = halfWidth * scale;
        const float outer = (halfWidth + kFeather) * scale;
        const float x = float(p[i].x()), y = float(p[i].y());
        const float nx = float(mitre.x()), ny = float(mitre.y());
        setColoredVertex(v[4 * i + 0], x - nx * outer, y - ny * outer, color, 0.0f);
        setColoredVertex(v[4 * i + 1], x - nx * inner, y - ny * inner, color, alpha);
        setColoredVertex(v[4 * i + 2], x + nx * inner, y + ny * inner, color, alpha);
        setColoredVertex(v[4 * i + 3], x + nx * outer, y + ny * outer, color, 0.0f);
    }

    auto* idx = g->indexDataAsUShort();
    int k = 0;
    for (int i = 0; i < count - 1; ++i) {
        for (int band = 0; band < 3; ++band) {
            const auto a = static_cast<quint16>(4 * i + band);
            const auto b = static_cast<quint16>(a + 1);
            const auto c = static_cast<quint16>(a + 4);
            const auto d = static_cast<quint16>(c + 1);
            idx[k++] = a; idx[k++] = b; idx[k++] = c;
            idx[k++] = b; idx[k++] = d; idx[k++] = c;
        }
    }
}

} // namespace

WaveCanvas::WaveCanvas(QQuickItem* parent)
    : QQuickItem(parent)
    , points_(kPointCount, 0.0f)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    // Handle touch directly (rather than as synthesized mouse events) so we
    // can tell one finger from two.
    setAcceptTouchEvents(true);
    loadPreset(QStringLiteral("sine"));
}

QList<qreal> WaveCanvas::samples() const
{
    return QList<qreal>(points_.begin(), points_.end());
}

void WaveCanvas::setLineColor(const QColor& color)
{
    if (color == lineColor_)
        return;
    lineColor_ = color;
    waveDirty_ = true;
    update();
    emit appearanceChanged();
}

void WaveCanvas::setGridColor(const QColor& color)
{
    if (color == gridColor_)
        return;
    gridColor_ = color;
    gridDirty_ = true;
    update();
    emit appearanceChanged();
}

void WaveCanvas::setLineWidth(qreal width)
{
    if (qFuzzyCompare(width, lineWidth_))
        return;
    lineWidth_ = width;
    waveDirty_ = gridDirty_ = true;
    update();
    emit appearanceChanged();
}

// ---- Actions ---------------------------------------------------------------

void WaveCanvas::loadPreset(const QString& name)
{
    const int last = kPointCount - 1;
    for (int i = 0; i < kPointCount; ++i) {
        const float t = static_cast<float>(i) / last; // 0..1 across one cycle
        float v = 0.0f;
        if (name == QLatin1String("sine"))
            v = std::sin(2.0f * static_cast<float>(M_PI) * t);
        else if (name == QLatin1String("triangle"))
            v = t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f);
        else if (name == QLatin1String("saw"))
            v = 2.0f * t - 1.0f;
        else if (name == QLatin1String("square"))
            v = t < 0.5f ? 1.0f : -1.0f;
        points_[i] = v;
    }
    shapeEdited();
}

void WaveCanvas::clear()
{
    std::fill(points_.begin(), points_.end(), 0.0f);
    shapeEdited();
}

void WaveCanvas::rotatePhase(int steps)
{
    if (steps == 0)
        return;
    hw::rotatePhase(points_, steps);
    shapeEdited();
}

void WaveCanvas::joinEnds(qreal blendFraction)
{
    hw::joinEnds(points_, static_cast<float>(blendFraction));
    shapeEdited();
}

void WaveCanvas::shapeEdited()
{
    waveDirty_ = true;
    update();
    emit samplesChanged();
}

// ---- Input -----------------------------------------------------------------

void WaveCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
        beginScroll(event->position().x());
    else
        beginDraw(event->position());
    event->accept();
}

void WaveCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (gesture_ == Gesture::Draw)
        drawTo(event->position());
    else if (gesture_ == Gesture::Scroll)
        scrollTo(event->position().x());
    event->accept();
}

void WaveCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (gesture_ == Gesture::Draw)
        drawTo(event->position());
    endGesture();
    event->accept();
}

void WaveCanvas::mouseUngrabEvent()
{
    endGesture();
}

void WaveCanvas::touchEvent(QTouchEvent* event)
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

void WaveCanvas::touchUngrabEvent()
{
    endGesture();
}

void WaveCanvas::beginDraw(const QPointF& position)
{
    gesture_ = Gesture::Draw;
    strokeSnapshot_ = points_;
    strokeTimer_.start();
    lastIndex_ = -1;
    drawTo(position);
}

void WaveCanvas::drawTo(const QPointF& position)
{
    const QRectF area = plotArea();
    if (area.width() <= 0 || area.height() <= 0)
        return;

    const int last = kPointCount - 1;
    const int index = std::clamp(qRound((position.x() - area.left()) / area.width() * last), 0, last);
    const float value = std::clamp(
        static_cast<float>(1.0 - 2.0 * (position.y() - area.top()) / area.height()), -1.0f, 1.0f);

    if (lastIndex_ < 0 || lastIndex_ == index) {
        points_[index] = value;
    } else {
        // A fast stroke skips columns between two events. Fill them with a
        // straight line so the drawing never has gaps.
        const int step = index > lastIndex_ ? 1 : -1;
        const int span = std::abs(index - lastIndex_);
        for (int k = 1; k <= span; ++k) {
            const float t = static_cast<float>(k) / span;
            points_[lastIndex_ + k * step] = lastValue_ + (value - lastValue_) * t;
        }
    }
    lastIndex_ = index;
    lastValue_ = value;
    shapeEdited();
}

void WaveCanvas::beginScroll(qreal x)
{
    if (gesture_ == Gesture::Draw && strokeTimer_.isValid()
        && strokeTimer_.elapsed() < kSecondFingerGraceMs) {
        points_ = strokeSnapshot_;
        shapeEdited();
    }
    gesture_ = Gesture::Scroll;
    scrollAnchorX_ = x;
}

void WaveCanvas::scrollTo(qreal x)
{
    const qreal pixelsPerPoint = plotArea().width() / (kPointCount - 1);
    if (pixelsPerPoint <= 0)
        return;
    // Move by whole points only, carrying the remainder, so the drawing tracks
    // the finger exactly however slowly it moves.
    const int steps = static_cast<int>((x - scrollAnchorX_) / pixelsPerPoint);
    if (steps != 0) {
        rotatePhase(steps);
        scrollAnchorX_ += steps * pixelsPerPoint;
    }
}

void WaveCanvas::endGesture()
{
    gesture_ = Gesture::None;
    strokeTimer_.invalidate();
}

// ---- Rendering -------------------------------------------------------------

void WaveCanvas::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        waveDirty_ = gridDirty_ = true;
        update();
    }
}

QRectF WaveCanvas::plotArea() const
{
    // Inset so a full-scale value's line isn't clipped at the edge, and leave
    // room on the right for the ghost of the next cycle.
    const qreal inset = lineWidth_ + kFeather;
    return QRectF(0, inset, width() / (1.0 + kGhostFraction),
                  std::max<qreal>(0, height() - 2 * inset));
}

qreal WaveCanvas::valueToY(float value, const QRectF& area) const
{
    return area.top() + (1.0 - value) * 0.5 * area.height();
}

QSGNode* WaveCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    // This runs on the render thread while the GUI thread is blocked, so
    // reading points_ here is safe.
    QSGNode* root = oldNode;
    if (!root) {
        root = new QSGNode;

        auto* gridGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        gridGeometry->setDrawingMode(QSGGeometry::DrawLines);
        gridGeometry->setLineWidth(1);
        root->appendChildNode(makeNode(gridGeometry, new QSGFlatColorMaterial));

        // Soft gradient between the zero line and the curve.
        auto* fillGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        fillGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        root->appendChildNode(makeNode(fillGeometry, new QSGVertexColorMaterial));

        root->appendChildNode(makeNode(makeRibbonGeometry(), new QSGVertexColorMaterial)); // ghost
        root->appendChildNode(makeNode(makeRibbonGeometry(), new QSGVertexColorMaterial)); // line

        gridDirty_ = waveDirty_ = true;
    }

    const QRectF area = plotArea();
    const float halfWidth = static_cast<float>(lineWidth_ * 0.5);

    if (gridDirty_) {
        auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(GridNode));
        static_cast<QSGFlatColorMaterial*>(node->material())->setColor(gridColor_);

        // Vertical lines at quarter-cycles plus the seam at the right edge;
        // horizontal lines at -1, -0.5, 0, 0.5, 1.
        QSGGeometry* g = node->geometry();
        g->allocate(2 * (4 + 5));
        auto* v = g->vertexDataAsPoint2D();
        int n = 0;
        for (int i = 1; i <= 4; ++i) {
            const float x = static_cast<float>(area.left() + area.width() * i / 4.0);
            v[n++].set(x, static_cast<float>(area.top()));
            v[n++].set(x, static_cast<float>(area.bottom()));
        }
        for (int i = 0; i <= 4; ++i) {
            const float y = static_cast<float>(area.top() + area.height() * i / 4.0);
            v[n++].set(static_cast<float>(area.left()), y);
            v[n++].set(static_cast<float>(area.right()), y);
        }
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        gridDirty_ = false;
    }

    if (waveDirty_) {
        const qreal dx = area.width() / (kPointCount - 1);

        // Screen positions of every point.
        std::vector<QPointF> p(kPointCount);
        for (int i = 0; i < kPointCount; ++i)
            p[i] = QPointF(area.left() + dx * i, valueToY(points_[i], area));
        const float zeroY = static_cast<float>(valueToY(0.0f, area));

        // Fill: one vertex on the curve, one on the zero line, per point.
        {
            auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(FillNode));
            QSGGeometry* g = node->geometry();
            if (g->vertexCount() != 2 * kPointCount)
                g->allocate(2 * kPointCount);
            auto* v = g->vertexDataAsColoredPoint2D();
            for (int i = 0; i < kPointCount; ++i) {
                setColoredVertex(v[2 * i], float(p[i].x()), float(p[i].y()), lineColor_, 0.22f);
                setColoredVertex(v[2 * i + 1], float(p[i].x()), zeroY, lineColor_, 0.0f);
            }
            node->markDirty(QSGNode::DirtyGeometry);
        }

        // Ghost: from the end of this cycle, (jump to) the start of the next one.
        {
            const int ghostPoints = qRound(kGhostFraction * (kPointCount - 1));
            std::vector<QPointF> ghost;
            ghost.reserve(ghostPoints + 2);
            ghost.push_back(p.back());
            const QPointF nextStart(area.right(), p.front().y());
            if (std::abs(nextStart.y() - p.back().y()) > 0.5) // the seam jump, if any
                ghost.push_back(nextStart);
            for (int i = 1; i <= ghostPoints; ++i)
                ghost.emplace_back(area.right() + dx * i, p[i].y());

            auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(GhostNode));
            buildRibbon(node->geometry(), ghost, halfWidth, lineColor_, 0.35f);
            node->markDirty(QSGNode::DirtyGeometry);
        }

        {
            auto* node = static_cast<QSGGeometryNode*>(root->childAtIndex(LineNode));
            buildRibbon(node->geometry(), p, halfWidth, lineColor_, 1.0f);
            node->markDirty(QSGNode::DirtyGeometry);
        }
        waveDirty_ = false;
    }

    return root;
}
