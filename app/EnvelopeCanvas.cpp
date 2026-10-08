#include "EnvelopeCanvas.h"

#include "SceneGraphHelpers.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
#include <cmath>

namespace {

// Children of the root node, in back-to-front order.
enum NodeIndex {
    GridNode, GuideNode,                          // flat-colour lines
    AttackFill, DecayFill, ReleaseFill,           // gradients under the curve
    SustainLine, AttackLine, DecayLine, ReleaseLine,
    HandleNode, PlayheadNode,
    NodeCount
};

constexpr qreal kTabZone = 20.0;      // strip above the plot holding the divider tabs
constexpr qreal kGrabRadius = 32.0;   // how close a press must be to grab a handle
constexpr float kHandleRadius = 6.0f;
constexpr qreal kSustainShare = 0.2;  // ADSR: share of the width showing the sustain stretch
constexpr int kSegmentSamples = 60;   // ADSR: points drawn per segment

} // namespace

EnvelopeCanvas::EnvelopeCanvas(QQuickItem* parent)
    : CurveCanvas(hw::kEnvelopePoints, 0.0f, 1.0f, parent)
{
    // Every drawing change goes straight to the envelope.
    connect(this, &CurveCanvas::samplesChanged, this, [this] {
        if (envelope_)
            envelope_->setCurve(samples());
        emit sustainLevelChanged();
    });
}

void EnvelopeCanvas::setEnvelope(EnvelopeController* envelope)
{
    if (envelope == envelope_)
        return;
    if (envelope_)
        envelope_->disconnect(this);
    envelope_ = envelope;
    if (envelope_) {
        connect(envelope_, &EnvelopeController::settingsChanged, this, &EnvelopeCanvas::settingsChanged);
        connect(envelope_, &EnvelopeController::statusChanged, this, [this] {
            qCDebug(lcEnvelope) << "canvas" << this << "update requested; visible" << isVisible();
            update();
        });
        envelope_->setCurve(samples());
        settingsChanged();
    }
    emit envelopeChanged();
}

qreal EnvelopeCanvas::sustainLevel() const
{
    hw::EnvelopeCurve curve;
    curve.points = points_;
    return envelope_ ? curve.sample(envelope_->split2()) : 0.0;
}

bool EnvelopeCanvas::drawnMode() const
{
    return envelope_ && envelope_->mode() == int(hw::EnvelopeMode::Drawn);
}

void EnvelopeCanvas::settingsChanged()
{
    // First time in Drawn mode: start the drawing from the ADSR you've been hearing.
    if (drawnMode() && !seeded_) {
        seeded_ = true;
        hw::EnvelopeCurve curve;
        hw::DrawnTiming timing = envelope_->timing();
        hw::renderAdsrToDrawing(envelope_->adsr(), curve, timing);
        points_ = curve.points;
        envelope_->setTiming(timing);
        shapeEdited();
    }
    curveDirty_ = true;
    update();
    emit sustainLevelChanged(); // the divider may have moved
}

// ---- Layout ------------------------------------------------------------------

QRectF EnvelopeCanvas::plotArea() const
{
    const qreal inset = lineWidth_ + kHandleRadius;
    return QRectF(inset, kTabZone, std::max<qreal>(0, width() - 2 * inset),
                  std::max<qreal>(0, height() - kTabZone - inset));
}

EnvelopeCanvas::AdsrLayout EnvelopeCanvas::adsrLayout() const
{
    const QRectF area = plotArea();
    const hw::AdsrParams& p = envelope_->adsr();
    const qreal timed = p.attack + p.decay + p.release;
    const qreal scale = frozenScale_ > 0 ? frozenScale_ : area.width() * (1.0 - kSustainShare) / timed;
    AdsrLayout l;
    l.pixelsPerSecond = scale;
    l.start = area.left();
    l.attackEnd = l.start + p.attack * scale;
    l.decayEnd = l.attackEnd + p.decay * scale;
    l.sustainEnd = l.decayEnd + area.width() * kSustainShare;
    l.releaseEnd = l.sustainEnd + p.release * scale;
    return l;
}

// ---- Handles -----------------------------------------------------------------

bool EnvelopeCanvas::beginCustomDrag(const QPointF& pos)
{
    if (!envelope_)
        return true; // nothing to draw on
    const QRectF area = plotArea();
    auto near = [&](QPointF handle) { return QLineF(handle, pos).length() <= kGrabRadius; };

    if (drawnMode()) {
        // Only the tab strip along the top grabs dividers; the rest is for drawing.
        if (pos.y() > area.top() + 6)
            return false;
        const qreal x1 = area.left() + envelope_->split1() * area.width();
        const qreal x2 = area.left() + envelope_->split2() * area.width();
        const qreal d1 = std::abs(pos.x() - x1), d2 = std::abs(pos.x() - x2);
        if (std::min(d1, d2) > kGrabRadius)
            return false;
        dragging_ = d1 <= d2 ? Handle::Split1 : Handle::Split2;
        return true;
    }

    // ADSR mode: no drawing; grab the nearest handle, if any.
    const AdsrLayout l = adsrLayout();
    const QPointF peak(l.attackEnd, valueToY(1.0f, area));
    const QPointF decayEnd(l.decayEnd, valueToY(float(envelope_->sustain()), area));
    const QPointF releaseEnd(l.releaseEnd, valueToY(0.0f, area));
    dragging_ = Handle::None;
    qreal best = kGrabRadius;
    for (auto [handle, point] : {std::pair{Handle::AttackPeak, peak}, std::pair{Handle::DecayEnd, decayEnd},
                                 std::pair{Handle::ReleaseEnd, releaseEnd}}) {
        const qreal d = QLineF(point, pos).length();
        if (near(point) && d <= best) {
            best = d;
            dragging_ = handle;
        }
    }
    frozenScale_ = l.pixelsPerSecond;
    return true;
}

void EnvelopeCanvas::customDragTo(const QPointF& pos)
{
    if (!envelope_ || dragging_ == Handle::None)
        return;
    const QRectF area = plotArea();
    const qreal fraction = (pos.x() - area.left()) / area.width();
    switch (dragging_) {
    case Handle::Split1:
        envelope_->setSplit1(fraction);
        break;
    case Handle::Split2:
        envelope_->setSplit2(fraction);
        break;
    case Handle::AttackPeak: {
        const AdsrLayout l = adsrLayout();
        envelope_->setAttack((pos.x() - l.start) / l.pixelsPerSecond);
        break;
    }
    case Handle::DecayEnd: {
        const AdsrLayout l = adsrLayout();
        envelope_->setDecay((pos.x() - l.attackEnd) / l.pixelsPerSecond);
        envelope_->setSustain(yToValue(pos.y(), area));
        break;
    }
    case Handle::ReleaseEnd: {
        const AdsrLayout l = adsrLayout();
        envelope_->setRelease((pos.x() - l.sustainEnd) / l.pixelsPerSecond);
        break;
    }
    case Handle::None:
        break;
    }
}

void EnvelopeCanvas::endCustomDrag()
{
    dragging_ = Handle::None;
    frozenScale_ = 0.0; // let the time axis fit the envelope again
    curveDirty_ = true;
    update();
}

// ---- Rendering ---------------------------------------------------------------

QSGNode* EnvelopeCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    qCDebug(lcEnvelope) << "canvas" << this << "paint; stage" << (envelope_ ? envelope_->stage() : -1);
    QSGNode* root = oldNode;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(sg::makeNode(sg::makeLines(), new QSGFlatColorMaterial)); // grid
        root->appendChildNode(sg::makeNode(sg::makeLines(), new QSGFlatColorMaterial)); // guides
        for (int i = AttackFill; i < NodeCount; ++i)
            root->appendChildNode(sg::makeNode(sg::makeColoredTriangles(), new QSGVertexColorMaterial));
        gridDirty_ = curveDirty_ = true;
    }
    auto child = [root](int index) { return static_cast<QSGGeometryNode*>(root->childAtIndex(index)); };
    const QRectF area = plotArea();
    const float halfWidth = float(lineWidth_ * 0.5);
    const bool drawn = drawnMode();

    if (gridDirty_) {
        std::vector<QPointF> lines;
        for (float level : {0.0f, 0.5f, 1.0f}) {
            const qreal y = valueToY(level, area);
            lines.insert(lines.end(), {QPointF(area.left(), y), QPointF(area.right(), y)});
        }
        QSGGeometryNode* node = child(GridNode);
        static_cast<QSGFlatColorMaterial*>(node->material())->setColor(gridColor_);
        sg::buildLines(node->geometry(), lines);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        gridDirty_ = false;
    }

    if (curveDirty_ && envelope_) {
        sg::Polyline attack, decay, sustain, release;
        std::vector<QPointF> guides, handles;
        auto dashed = [&](qreal x) { // dotted vertical line, top to bottom
            for (qreal y = area.top(); y < area.bottom(); y += 9.0)
                guides.insert(guides.end(), {QPointF(x, y), QPointF(x, std::min(y + 4.0, area.bottom()))});
        };

        if (drawn) {
            // The drawing, split into its three coloured segments.
            const int last = hw::kEnvelopePoints - 1;
            const int i1 = int(std::lround(envelope_->split1() * last));
            const int i2 = int(std::lround(envelope_->split2() * last));
            auto point = [&](int i) { return QPointF(area.left() + area.width() * i / last, valueToY(points_[i], area)); };
            for (int i = 0; i <= i1; ++i) attack.push_back(point(i));
            for (int i = i1; i <= i2; ++i) decay.push_back(point(i));
            for (int i = i2; i <= last; ++i) release.push_back(point(i));
            for (int i : {i1, i2}) {
                const qreal x = area.left() + area.width() * i / last;
                dashed(x);
                handles.emplace_back(x, area.top() - kTabZone / 2);
            }
        } else {
            // The ADSR shape from the same maths the audio uses.
            const hw::AdsrParams& p = envelope_->adsr();
            const AdsrLayout l = adsrLayout();
            auto segment = [&](sg::Polyline& out, hw::EnvelopeStage stage, double seconds, double startLevel,
                               qreal x0, qreal x1) {
                for (int s = 0; s <= kSegmentSamples; ++s) {
                    const double u = double(s) / kSegmentSamples;
                    const double level = hw::adsrLevel(p, stage, u * seconds, startLevel);
                    out.emplace_back(x0 + (x1 - x0) * u, valueToY(float(level), area));
                }
            };
            segment(attack, hw::EnvelopeStage::Attack, p.attack, 0.0, l.start, l.attackEnd);
            segment(decay, hw::EnvelopeStage::Decay, p.decay, 1.0, l.attackEnd, l.decayEnd);
            const qreal sustainY = valueToY(float(p.sustain), area);
            sustain = {QPointF(l.decayEnd, sustainY), QPointF(l.sustainEnd, sustainY)};
            segment(release, hw::EnvelopeStage::Release, p.release, p.sustain, l.sustainEnd, l.releaseEnd);
            for (qreal x : {l.attackEnd, l.decayEnd, l.sustainEnd})
                dashed(x);
            handles = {QPointF(l.attackEnd, valueToY(1.0f, area)), QPointF(l.decayEnd, sustainY),
                       QPointF(l.releaseEnd, valueToY(0.0f, area))};
        }

        const float baseline = float(area.bottom());
        sg::buildFill(child(AttackFill)->geometry(), attack, baseline, attackColor_, 0.18f);
        sg::buildFill(child(DecayFill)->geometry(), decay, baseline, decayColor_, 0.18f);
        sg::buildFill(child(ReleaseFill)->geometry(), release, baseline, releaseColor_, 0.18f);
        sg::buildRibbons(child(SustainLine)->geometry(), {sustain}, halfWidth, decayColor_, 0.4f);
        sg::buildRibbons(child(AttackLine)->geometry(), {attack}, halfWidth, attackColor_, 1.0f);
        sg::buildRibbons(child(DecayLine)->geometry(), {decay}, halfWidth, decayColor_, 1.0f);
        sg::buildRibbons(child(ReleaseLine)->geometry(), {release}, halfWidth, releaseColor_, 1.0f);
        sg::buildDots(child(HandleNode)->geometry(), handles, kHandleRadius, handleColor_);

        QSGGeometryNode* guideNode = child(GuideNode);
        static_cast<QSGFlatColorMaterial*>(guideNode->material())->setColor(gridColor_.lighter(180));
        sg::buildLines(guideNode->geometry(), guides);

        for (int n = GuideNode; n < PlayheadNode; ++n)
            child(n)->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        curveDirty_ = false;
    }

    // Playhead: where a sounding note is on the envelope.
    std::vector<QPointF> playhead;
    if (envelope_ && envelope_->stage() != int(hw::EnvelopeStage::Idle)) {
        const auto stage = hw::EnvelopeStage(envelope_->stage());
        const double t = envelope_->stageSeconds();
        auto progress = [t](double seconds) { return std::clamp(t / std::max(seconds, 1e-3), 0.0, 1.0); };
        qreal x = area.left();
        if (drawn) {
            const hw::DrawnTiming& d = envelope_->timing();
            double f = 0.0;
            switch (stage) {
            case hw::EnvelopeStage::Attack: f = progress(d.attackSeconds()) * d.split1; break;
            case hw::EnvelopeStage::Decay: f = d.split1 + progress(d.decaySeconds()) * (d.split2 - d.split1); break;
            case hw::EnvelopeStage::Sustain: f = d.split2; break;
            case hw::EnvelopeStage::Release: f = d.split2 + progress(d.releaseSeconds()) * (1.0 - d.split2); break;
            default: break;
            }
            x = area.left() + f * area.width();
        } else {
            const hw::AdsrParams& p = envelope_->adsr();
            const AdsrLayout l = adsrLayout();
            switch (stage) {
            case hw::EnvelopeStage::Attack: x = l.start + progress(p.attack) * (l.attackEnd - l.start); break;
            case hw::EnvelopeStage::Decay: x = l.attackEnd + progress(p.decay) * (l.decayEnd - l.attackEnd); break;
            case hw::EnvelopeStage::Sustain: x = (l.decayEnd + l.sustainEnd) / 2; break;
            case hw::EnvelopeStage::Release: x = l.sustainEnd + progress(p.release) * (l.releaseEnd - l.sustainEnd); break;
            default: break;
            }
        }
        playhead.emplace_back(x, valueToY(float(envelope_->level()), area));
    }
    sg::buildDots(child(PlayheadNode)->geometry(), playhead, kHandleRadius - 1.0f, Qt::white);
    child(PlayheadNode)->markDirty(QSGNode::DirtyGeometry);

    return root;
}
