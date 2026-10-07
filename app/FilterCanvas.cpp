#include "FilterCanvas.h"

#include "SceneGraphHelpers.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>

namespace {

// Children of the root node, in back-to-front order.
enum NodeIndex { GridNode = 0, AxisNode = 1, FillNode = 2, ExtensionNode = 3, LineNode = 4,
                 ResponseNode = 5, MarkerNode = 6 };

// Display samples for the curve and response lines.
constexpr int kDisplayPoints = 240;
constexpr float kMarkerRadius = 5.0f;

} // namespace

FilterCanvas::FilterCanvas(QQuickItem* parent)
    : CurveCanvas(hw::kFilterCurvePoints, hw::kFilterMinDb, hw::kFilterMaxDb, parent)
    , designer_(std::make_unique<hw::FilterDesigner>(48000.0))
{
    loadPreset(QStringLiteral("flat"));
}

hw::FilterCurve FilterCanvas::curve() const
{
    hw::FilterCurve c;
    c.db = points_;
    return c;
}

// ---- Properties --------------------------------------------------------------

void FilterCanvas::settingsEdited()
{
    curveDirty_ = true;
    update();
    emit settingsChanged();
}

void FilterCanvas::setShiftOctaves(qreal octaves)
{
    octaves = std::clamp<qreal>(octaves, -kMaxShiftOctaves, kMaxShiftOctaves);
    if (octaves == settings_.shiftOctaves)
        return;
    settings_.shiftOctaves = octaves;
    settingsEdited();
}

void FilterCanvas::setLoop(bool on)
{
    if (on == settings_.loop)
        return;
    settings_.loop = on;
    settingsEdited();
}

void FilterCanvas::setResonanceX(qreal x)
{
    x = std::clamp<qreal>(x, 0.0, 1.0);
    if (x == settings_.resonanceX)
        return;
    settings_.resonanceX = x;
    settingsEdited();
}

void FilterCanvas::setResonanceDb(qreal db)
{
    db = std::clamp<qreal>(db, hw::kFilterMinDb, hw::kFilterMaxDb);
    if (db == settings_.resonanceDb)
        return;
    settings_.resonanceDb = db;
    settingsEdited();
}

qreal FilterCanvas::resonanceHz() const
{
    const auto x = hw::curveToDisplay(settings_.resonanceX, settings_);
    return x ? hw::filterXToHz(*x) : 0.0;
}

qreal FilterCanvas::sampleRate() const
{
    return designer_->sampleRate();
}

void FilterCanvas::setSampleRate(qreal rate)
{
    if (rate <= 0 || rate == designer_->sampleRate())
        return;
    designer_ = std::make_unique<hw::FilterDesigner>(rate);
    settingsEdited();
}

void FilterCanvas::setResponseColor(const QColor& color)
{
    if (color == responseColor_)
        return;
    responseColor_ = color;
    markDirty();
    emit appearanceChanged();
}

// ---- Actions -----------------------------------------------------------------

void FilterCanvas::loadPreset(const QString& name)
{
    hw::FilterPreset preset = hw::FilterPreset::Flat;
    if (name == QLatin1String("lowpass"))
        preset = hw::FilterPreset::LowPass;
    else if (name == QLatin1String("highpass"))
        preset = hw::FilterPreset::HighPass;
    else if (name == QLatin1String("bandpass"))
        preset = hw::FilterPreset::BandPass;

    double resonanceX = settings_.resonanceX;
    points_ = hw::FilterCurve::preset(preset, &resonanceX).db;
    settings_.resonanceX = resonanceX;
    settings_.resonanceDb = curve().sample(resonanceX); // no peak until dialled in
    shapeEdited();
    settingsEdited();
}

void FilterCanvas::resetResonance()
{
    setResonanceDb(curve().sample(settings_.resonanceX));
}

void FilterCanvas::moveResonance(qreal semitones)
{
    setResonanceX(settings_.resonanceX + semitones / 12.0 / hw::filterOctaveSpan());
}

qreal FilterCanvas::xForHz(qreal hz) const
{
    const QRectF area = plotArea();
    return area.left() + hw::filterHzToX(hz) * area.width();
}

qreal FilterCanvas::yForDb(qreal db) const
{
    return valueToY(float(db), plotArea());
}

// ---- Input -------------------------------------------------------------------

std::optional<double> FilterCanvas::curvePositionAt(double plotX) const
{
    return hw::displayToCurve(plotX, settings_);
}

void FilterCanvas::strokeEdited(int firstIndex, int lastIndex)
{
    // Drawing takes priority: crossing the resonance point sets its level.
    const int resonanceIndex = int(std::lround(settings_.resonanceX * (hw::kFilterCurvePoints - 1)));
    if (resonanceIndex >= firstIndex && resonanceIndex <= lastIndex)
        setResonanceDb(points_[resonanceIndex]);
}

void FilterCanvas::scrollByPixels(qreal dx)
{
    const QRectF area = plotArea();
    if (area.width() > 0)
        setShiftOctaves(settings_.shiftOctaves + dx / area.width() * hw::filterOctaveSpan());
}

// ---- Rendering ---------------------------------------------------------------

QRectF FilterCanvas::plotArea() const
{
    const qreal inset = lineWidth_ + kMarkerRadius;
    return QRectF(0, inset, width(), std::max<qreal>(0, height() - 2 * inset));
}

QSGNode* FilterCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    QSGNode* root = oldNode;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(sg::makeNode(sg::makeLines(), new QSGFlatColorMaterial)); // grid
        root->appendChildNode(sg::makeNode(sg::makeLines(), new QSGFlatColorMaterial)); // 0 dB + decades
        for (int i = FillNode; i <= MarkerNode; ++i)
            root->appendChildNode(sg::makeNode(sg::makeColoredTriangles(), new QSGVertexColorMaterial));
        gridDirty_ = curveDirty_ = true;
    }
    auto child = [root](int index) { return static_cast<QSGGeometryNode*>(root->childAtIndex(index)); };

    const QRectF area = plotArea();
    const float halfWidth = static_cast<float>(lineWidth_ * 0.5);
    auto xAt = [&](double displayX) { return area.left() + displayX * area.width(); };

    if (gridDirty_) {
        // Minor: every 12 dB, and 2x / 5x frequencies. Major: 0 dB and decades.
        std::vector<QPointF> minor, major;
        for (float db = hw::kFilterMinDb; db <= hw::kFilterMaxDb; db += 12.0f) {
            const qreal y = valueToY(db, area);
            auto& list = db == 0.0f ? major : minor;
            list.insert(list.end(), {QPointF(area.left(), y), QPointF(area.right(), y)});
        }
        for (double decade : {10.0, 100.0, 1000.0, 10000.0}) {
            for (double mult : {1.0, 2.0, 5.0}) {
                const double hz = decade * mult;
                if (hz < hw::kFilterMinHz || hz > hw::kFilterMaxHz)
                    continue;
                const qreal x = xAt(hw::filterHzToX(hz));
                auto& list = mult == 1.0 ? major : minor;
                list.insert(list.end(), {QPointF(x, area.top()), QPointF(x, area.bottom())});
            }
        }
        QSGGeometryNode* gridNode = child(GridNode);
        static_cast<QSGFlatColorMaterial*>(gridNode->material())->setColor(gridColor_);
        sg::buildLines(gridNode->geometry(), minor);
        QSGGeometryNode* axisNode = child(AxisNode);
        static_cast<QSGFlatColorMaterial*>(axisNode->material())->setColor(gridColor_.lighter(160));
        sg::buildLines(axisNode->geometry(), major);
        gridNode->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        axisNode->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        gridDirty_ = false;
    }

    if (curveDirty_) {
        const hw::FilterCurve c = curve();

        // The drawn curve as positioned by the cutoff, split into the parts
        // that are the curve itself and the dimmed extensions past its ends.
        std::vector<sg::Polyline> onCurve(1), extension(1);
        sg::Polyline all;
        bool previousOn = true;
        for (int i = 0; i < kDisplayPoints; ++i) {
            const double x = double(i) / (kDisplayPoints - 1);
            const QPointF p(xAt(x), valueToY(hw::targetDb(c, settings_, x), area));
            const bool on = hw::displayToCurve(x, settings_).has_value();
            if (i > 0 && on != previousOn) {
                // Switch lines, sharing the boundary point so there's no gap.
                auto& from = previousOn ? onCurve : extension;
                auto& to = on ? onCurve : extension;
                if (!to.back().empty())
                    to.emplace_back();
                to.back().push_back(from.back().back());
            }
            (on ? onCurve : extension).back().push_back(p);
            all.push_back(p);
            previousOn = on;
        }
        sg::buildFill(child(FillNode)->geometry(), all, float(area.bottom()), lineColor_, 0.18f);
        sg::buildRibbons(child(ExtensionNode)->geometry(), extension, halfWidth, lineColor_, 0.3f);
        sg::buildRibbons(child(LineNode)->geometry(), onCurve, halfWidth, lineColor_, 1.0f);

        // What the filter really does, including resonance.
        hw::FilterDesigner::Sections sections;
        designer_->design(c, settings_, sections);
        sg::Polyline response;
        for (int i = 0; i < kDisplayPoints; ++i) {
            const double x = double(i) / (kDisplayPoints - 1);
            const double db = std::clamp(designer_->responseDb(sections, hw::filterXToHz(x)),
                                         double(hw::kFilterMinDb), double(hw::kFilterMaxDb));
            response.emplace_back(xAt(x), valueToY(float(db), area));
        }
        sg::buildRibbons(child(ResponseNode)->geometry(), {response}, halfWidth * 0.5f, responseColor_, 0.55f);

        // Resonance marker, if it's on the display.
        if (const auto rx = hw::curveToDisplay(settings_.resonanceX, settings_))
            sg::buildDot(child(MarkerNode)->geometry(), QPointF(xAt(*rx), valueToY(float(settings_.resonanceDb), area)),
                         kMarkerRadius, responseColor_);
        else
            child(MarkerNode)->geometry()->allocate(0, 0);

        for (int n = FillNode; n <= MarkerNode; ++n)
            child(n)->markDirty(QSGNode::DirtyGeometry);
        curveDirty_ = false;
    }

    return root;
}
