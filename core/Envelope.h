#pragma once

#include <vector>

namespace hw {

// ---- Envelope shapes ---------------------------------------------------------
//
// An envelope gives a level (0..1) over the life of a note. Two modes:
//
//   Adsr  - the classic four controls: Attack time, Decay time, Sustain level,
//           Release time, with analog-style exponential curves.
//   Drawn - a hand-drawn curve spanning `timespan` seconds, cut into three
//           segments at split1 and split2 (fractions of the width):
//           attack [0, split1], decay [split1, split2], release [split2, 1].
//           The sustain level is where the drawing is at split2 (the end of
//           the decay), held for as long as the note is held.

constexpr int kEnvelopePoints = 512;

enum class EnvelopeMode { Adsr = 0, Drawn = 1 };
enum class EnvelopeStage { Idle = 0, Attack = 1, Decay = 2, Sustain = 3, Release = 4 };

struct AdsrParams {
    double attack = 0.005; // seconds
    double decay = 0.3;    // seconds
    double sustain = 0.8;  // level 0..1
    double release = 0.3;  // seconds
};

struct DrawnTiming {
    double split1 = 1.0 / 3.0;
    double split2 = 2.0 / 3.0;
    double timespan = 1.0; // seconds for the whole drawing

    double attackSeconds() const { return split1 * timespan; }
    double decaySeconds() const { return (split2 - split1) * timespan; }
    double releaseSeconds() const { return (1.0 - split2) * timespan; }
};

// The drawn curve: kEnvelopePoints levels evenly spaced across the timespan.
struct EnvelopeCurve {
    std::vector<float> points = std::vector<float>(kEnvelopePoints, 0.0f);
    // Level at position x (0..1 across the drawing), interpolated.
    float sample(double x) const;
};

// Shape of an exponential segment: 0 at u = 0 rising to 1 at u = 1, fast at
// first then easing in, like a capacitor charging. Higher curvature = more
// pronounced. Used by the audio and the display, so they always agree.
double segmentCurve(double u, double curvature);
constexpr double kAttackCurvature = 1.5;
constexpr double kDecayCurvature = 4.0; // also release

// ADSR level at `seconds` into a stage, starting from `startLevel`.
// (Sustain/Idle just return the sustain level / 0.)
double adsrLevel(const AdsrParams& p, EnvelopeStage stage, double seconds, double startLevel);

// Draw the ADSR as a curve, with splits and timespan to match, so switching to
// Drawn mode starts from what you hear.
void renderAdsrToDrawing(const AdsrParams& adsr, EnvelopeCurve& curve, DrawnTiming& timing);

// ---- Generator ---------------------------------------------------------------

// Produces the envelope sample by sample. Real-time safe (no allocation).
//
// Drawn mode follows the drawing, joined to the live level so the output
// never jumps:
//   attack  ramps from wherever the level is now onto the drawing (an offset
//           that fades across the attack), so retriggering is click-free;
//   decay   is the drawing, ending at the sustain level;
//   release from sustain is the drawing. Released early (mid attack or
//           decay), the drawn release is scaled to start from the current
//           level, keeping its shape. Drawings that end above 0 fade out in
//           a few ms.
class Envelope {
public:
    explicit Envelope(double sampleRate = 48000.0);

    void setSampleRate(double sampleRate);
    void setMode(EnvelopeMode mode) { mode_ = mode; }
    EnvelopeMode mode() const { return mode_; }
    void setAdsr(const AdsrParams& params) { adsr_ = params; }
    // The curve is not owned; it must stay valid while set.
    void setDrawn(const EnvelopeCurve* curve, const DrawnTiming& timing)
    {
        curve_ = curve;
        timing_ = timing;
    }

    void gateOn();
    void gateOff();
    float next();

    EnvelopeStage stage() const { return stage_; }
    double stageSeconds() const { return time_; }
    float level() const { return level_; }

private:
    void enter(EnvelopeStage stage);
    double stageDuration() const;
    double drawnLevel(double u) const;

    double sampleRate_;
    double dt_;
    EnvelopeMode mode_ = EnvelopeMode::Adsr;
    AdsrParams adsr_;
    const EnvelopeCurve* curve_ = nullptr;
    DrawnTiming timing_;

    EnvelopeStage stage_ = EnvelopeStage::Idle;
    double time_ = 0.0;       // seconds into the current stage
    float level_ = 0.0f;      // current output
    double startLevel_ = 0.0; // output when the stage began
    float tailLevel_ = 0.0f;  // release end level being faded to 0
};

} // namespace hw
