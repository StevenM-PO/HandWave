#include "WaveCanvas.h"

#include "SceneGraphHelpers.h"
#include "ShapeEdits.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// Children of the root node, in back-to-front order.
enum NodeIndex { GridNode = 0, FillNode = 1, GhostNode = 2, LineNode = 3 };

} // namespace

WaveCanvas::WaveCanvas(QQuickItem* parent)
    : CurveCanvas(kPointCount, -1.0f, 1.0f, parent)
{
    loadPreset(QStringLiteral("sine"));
}

// ---- Actions -----------------------------------------------------------------

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

// ---- Input -------------------------------------------------------------------

void WaveCanvas::scrollByPixels(qreal dx)
{
    const qreal pixelsPerPoint = plotArea().width() / (kPointCount - 1);
    if (pixelsPerPoint <= 0)
        return;
    // Move by whole points only, carrying the remainder, so the drawing tracks
    // the finger exactly however slowly it moves.
    scrollRemainder_ += dx;
    const int steps = static_cast<int>(scrollRemainder_ / pixelsPerPoint);
    if (steps != 0) {
        rotatePhase(steps);
        scrollRemainder_ -= steps * pixelsPerPoint;
    }
}

// ---- Rendering ---------------------------------------------------------------

QRectF WaveCanvas::plotArea() const
{
    // Inset so a full-scale value's line isn't clipped at the edge, and leave
    // room on the right for the ghost of the next cycle.
    const qreal inset = lineWidth_ + sg::kFeather;
    return QRectF(0, inset, width() / (1.0 + kGhostFraction), std::max<qreal>(0, height() - 2 * inset));
}

QSGNode* WaveCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    // This runs on the render thread while the GUI thread is blocked, so
    // reading points_ here is safe.
    QSGNode* root = oldNode;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(sg::makeNode(sg::makeLines(), new QSGFlatColorMaterial));
        root->appendChildNode(sg::makeNode(sg::makeColoredTriangles(), new QSGVertexColorMaterial)); // fill
        root->appendChildNode(sg::makeNode(sg::makeColoredTriangles(), new QSGVertexColorMaterial)); // ghost
        root->appendChildNode(sg::makeNode(sg::makeColoredTriangles(), new QSGVertexColorMaterial)); // line
        gridDirty_ = curveDirty_ = true;
    }
    auto child = [root](int index) { return static_cast<QSGGeometryNode*>(root->childAtIndex(index)); };

    const QRectF area = plotArea();
    const float halfWidth = static_cast<float>(lineWidth_ * 0.5);

    if (gridDirty_) {
        // Vertical lines at quarter-cycles plus the seam at the right edge;
        // horizontal lines at -1, -0.5, 0, 0.5, 1.
        std::vector<QPointF> lines;
        for (int i = 1; i <= 4; ++i) {
            const qreal x = area.left() + area.width() * i / 4.0;
            lines.insert(lines.end(), {QPointF(x, area.top()), QPointF(x, area.bottom())});
        }
        for (int i = 0; i <= 4; ++i) {
            const qreal y = area.top() + area.height() * i / 4.0;
            lines.insert(lines.end(), {QPointF(area.left(), y), QPointF(area.right(), y)});
        }
        QSGGeometryNode* node = child(GridNode);
        static_cast<QSGFlatColorMaterial*>(node->material())->setColor(gridColor_);
        sg::buildLines(node->geometry(), lines);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        gridDirty_ = false;
    }

    if (curveDirty_) {
        const qreal dx = area.width() / (kPointCount - 1);
        sg::Polyline p(kPointCount);
        for (int i = 0; i < kPointCount; ++i)
            p[i] = QPointF(area.left() + dx * i, valueToY(points_[i], area));

        sg::buildFill(child(FillNode)->geometry(), p, float(valueToY(0.0f, area)), lineColor_, 0.22f);

        // Ghost: from the end of this cycle, (jump to) the start of the next one.
        const int ghostPoints = qRound(kGhostFraction * (kPointCount - 1));
        sg::Polyline ghost{p.back()};
        const QPointF nextStart(area.right(), p.front().y());
        if (std::abs(nextStart.y() - p.back().y()) > 0.5) // the seam jump, if any
            ghost.push_back(nextStart);
        for (int i = 1; i <= ghostPoints; ++i)
            ghost.emplace_back(area.right() + dx * i, p[i].y());
        sg::buildRibbons(child(GhostNode)->geometry(), {ghost}, halfWidth, lineColor_, 0.35f);

        sg::buildRibbons(child(LineNode)->geometry(), {p}, halfWidth, lineColor_, 1.0f);

        for (int n : {FillNode, GhostNode, LineNode})
            child(n)->markDirty(QSGNode::DirtyGeometry);
        curveDirty_ = false;
    }

    return root;
}
