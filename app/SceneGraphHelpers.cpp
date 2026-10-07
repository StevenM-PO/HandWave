#include "SceneGraphHelpers.h"

#include <QSGGeometryNode>
#include <QSGMaterial>

#include <algorithm>
#include <cmath>

namespace sg {

namespace {

// Colors for QSGVertexColorMaterial must be premultiplied by alpha.
void setVertex(QSGGeometry::ColoredPoint2D& v, float x, float y, const QColor& c, float alpha)
{
    const float a = static_cast<float>(c.alphaF()) * alpha;
    v.set(x, y,
          static_cast<uchar>(c.red() * a), static_cast<uchar>(c.green() * a),
          static_cast<uchar>(c.blue() * a), static_cast<uchar>(255 * a));
}

void ensureSize(QSGGeometry* g, int vertices, int indices)
{
    if (g->vertexCount() != vertices || g->indexCount() != indices)
        g->allocate(vertices, indices);
}

} // namespace

QSGGeometryNode* makeNode(QSGGeometry* geometry, QSGMaterial* material)
{
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

QSGGeometry* makeColoredTriangles()
{
    // 16-bit indices: 32-bit ones need an optional extension on OpenGL ES 2
    // (the Pi 3's GPU). Every canvas stays far below 65536 vertices.
    auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedShortType);
    g->setDrawingMode(QSGGeometry::DrawTriangles);
    return g;
}

QSGGeometry* makeLines()
{
    auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    g->setDrawingMode(QSGGeometry::DrawLines);
    g->setLineWidth(1);
    return g;
}

void buildRibbons(QSGGeometry* g, const std::vector<Polyline>& lines, float halfWidth,
                  const QColor& color, float alpha)
{
    // Each point gets 4 vertices across the line (soft edge, solid, solid,
    // soft edge); neighbours are joined by 3 strips of quads.
    int vertexCount = 0, indexCount = 0;
    for (const Polyline& p : lines) {
        if (p.size() < 2)
            continue;
        vertexCount += 4 * int(p.size());
        indexCount += 18 * (int(p.size()) - 1);
    }
    ensureSize(g, vertexCount, indexCount);
    auto* v = g->vertexDataAsColoredPoint2D();
    auto* idx = g->indexDataAsUShort();
    int base = 0, k = 0;

    for (const Polyline& p : lines) {
        const int count = int(p.size());
        if (count < 2)
            continue;
        auto segmentNormal = [&](int a, int b) {
            const QPointF d = p[b] - p[a];
            const qreal len = std::hypot(d.x(), d.y());
            return len > 1e-6 ? QPointF(-d.y() / len, d.x() / len) : QPointF(0, 0);
        };
        for (int i = 0; i < count; ++i) {
            // Mitre join: average the neighbouring segment normals and lengthen
            // the offset so the line keeps its width at corners.
            const QPointF n0 = segmentNormal(std::max(i - 1, 0), std::max(i, 1));
            const QPointF n1 = segmentNormal(std::min(i, count - 2), std::min(i + 1, count - 1));
            const QPointF reference = !n1.isNull() ? n1 : n0;
            QPointF mitre = n0 + n1;
            const qreal mitreLen = std::hypot(mitre.x(), mitre.y());
            mitre = mitreLen > 1e-6 ? mitre / mitreLen : (reference.isNull() ? QPointF(0, -1) : reference);
            const qreal dot = reference.isNull() ? 1.0 : mitre.x() * reference.x() + mitre.y() * reference.y();
            const float scale = static_cast<float>(1.0 / std::max(dot, 0.35)); // cap spikes

            const float inner = halfWidth * scale;
            const float outer = (halfWidth + kFeather) * scale;
            const float x = float(p[i].x()), y = float(p[i].y());
            const float nx = float(mitre.x()), ny = float(mitre.y());
            setVertex(v[base + 4 * i + 0], x - nx * outer, y - ny * outer, color, 0.0f);
            setVertex(v[base + 4 * i + 1], x - nx * inner, y - ny * inner, color, alpha);
            setVertex(v[base + 4 * i + 2], x + nx * inner, y + ny * inner, color, alpha);
            setVertex(v[base + 4 * i + 3], x + nx * outer, y + ny * outer, color, 0.0f);
        }
        for (int i = 0; i < count - 1; ++i) {
            for (int band = 0; band < 3; ++band) {
                const quint16 a = quint16(base + 4 * i + band), b = a + 1, c = a + 4, d = c + 1;
                idx[k++] = a; idx[k++] = b; idx[k++] = c;
                idx[k++] = b; idx[k++] = d; idx[k++] = c;
            }
        }
        base += 4 * count;
    }
}

void buildFill(QSGGeometry* g, const Polyline& line, float baselineY, const QColor& color, float alpha)
{
    const int count = int(line.size());
    if (count < 2) {
        ensureSize(g, 0, 0);
        return;
    }
    ensureSize(g, 2 * count, 6 * (count - 1));
    auto* v = g->vertexDataAsColoredPoint2D();
    for (int i = 0; i < count; ++i) {
        setVertex(v[2 * i], float(line[i].x()), float(line[i].y()), color, alpha);
        setVertex(v[2 * i + 1], float(line[i].x()), baselineY, color, 0.0f);
    }
    auto* idx = g->indexDataAsUShort();
    int k = 0;
    for (int i = 0; i < count - 1; ++i) {
        const quint16 a = quint16(2 * i), b = a + 1, c = a + 2, d = a + 3;
        idx[k++] = a; idx[k++] = b; idx[k++] = c;
        idx[k++] = b; idx[k++] = d; idx[k++] = c;
    }
}

void buildDots(QSGGeometry* g, const std::vector<QPointF>& centres, float radius, const QColor& color)
{
    // Per dot: centre, then an inner (solid) and outer (transparent) ring.
    constexpr int kSegments = 20;
    constexpr int kVertices = 1 + 2 * kSegments;
    constexpr double kPi = 3.14159265358979323846;
    const int dots = int(centres.size());
    ensureSize(g, dots * kVertices, dots * 9 * kSegments);
    auto* v = g->vertexDataAsColoredPoint2D();
    auto* idx = g->indexDataAsUShort();
    int k = 0;
    for (int d = 0; d < dots; ++d) {
        const int base = d * kVertices;
        const float cx = float(centres[d].x()), cy = float(centres[d].y());
        setVertex(v[base], cx, cy, color, 1.0f);
        for (int s = 0; s < kSegments; ++s) {
            const double a = 2.0 * kPi * s / kSegments;
            const float dx = float(std::cos(a)), dy = float(std::sin(a));
            setVertex(v[base + 1 + s], cx + dx * radius, cy + dy * radius, color, 1.0f);
            setVertex(v[base + 1 + kSegments + s], cx + dx * (radius + kFeather), cy + dy * (radius + kFeather),
                      color, 0.0f);
        }
        for (int s = 0; s < kSegments; ++s) {
            const quint16 i0 = quint16(base + 1 + s), i1 = quint16(base + 1 + (s + 1) % kSegments);
            const quint16 o0 = quint16(i0 + kSegments), o1 = quint16(i1 + kSegments);
            idx[k++] = quint16(base); idx[k++] = i0; idx[k++] = i1;
            idx[k++] = i0; idx[k++] = o0; idx[k++] = i1;
            idx[k++] = i1; idx[k++] = o0; idx[k++] = o1;
        }
    }
}

void buildLines(QSGGeometry* g, const std::vector<QPointF>& endpoints)
{
    if (g->vertexCount() != int(endpoints.size()))
        g->allocate(int(endpoints.size()));
    auto* v = g->vertexDataAsPoint2D();
    for (size_t i = 0; i < endpoints.size(); ++i)
        v[i].set(float(endpoints[i].x()), float(endpoints[i].y()));
}

} // namespace sg
