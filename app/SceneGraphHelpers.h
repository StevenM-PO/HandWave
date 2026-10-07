#pragma once

#include <QColor>
#include <QPointF>
#include <QSGGeometry>
#include <vector>

class QSGGeometryNode;
class QSGMaterial;

// Building blocks for the canvases' GPU-rendered (scene graph) drawing.
namespace sg {

using Polyline = std::vector<QPointF>;

// Soft edge (in pixels) added around lines and dots for anti-aliasing.
constexpr float kFeather = 1.0f;

// A geometry node that owns its geometry and material.
QSGGeometryNode* makeNode(QSGGeometry* geometry, QSGMaterial* material);
// Empty per-vertex-colored triangle geometry, for ribbons, fills and dots.
QSGGeometry* makeColoredTriangles();
// Empty flat-colored line geometry, for grids.
QSGGeometry* makeLines();

// Thick anti-aliased lines through each polyline. GPU line primitives can't
// reliably draw thick, smooth lines, so they're built as triangle ribbons.
void buildRibbons(QSGGeometry* g, const std::vector<Polyline>& lines, float halfWidth,
                  const QColor& color, float alpha);
// A soft gradient from each polyline (at `alpha`) down/up to `baselineY` (transparent).
void buildFill(QSGGeometry* g, const Polyline& line, float baselineY, const QColor& color, float alpha);
// A filled, anti-aliased dot.
void buildDot(QSGGeometry* g, QPointF centre, float radius, const QColor& color);
// Straight segments: each pair of points is one line.
void buildLines(QSGGeometry* g, const std::vector<QPointF>& endpoints);

} // namespace sg
