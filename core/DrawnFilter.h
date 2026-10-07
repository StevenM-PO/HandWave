#pragma once

#include "Biquad.h"

#include <array>
#include <optional>
#include <vector>

namespace hw {

// ---- The drawn curve ---------------------------------------------------------
//
// The filter screen's horizontal axis is log frequency, 20 Hz (x = 0) to
// 20 kHz (x = 1); the vertical axis is gain in dB. The user draws a curve in
// that space, which is stored independently of the cutoff: the cutoff shifts
// where the curve sits along the axis without ever editing it.

constexpr int kFilterCurvePoints = 512;
constexpr double kFilterMinHz = 20.0;
constexpr double kFilterMaxHz = 20000.0;
constexpr float kFilterMinDb = -48.0f;
constexpr float kFilterMaxDb = 24.0f;

// Octaves spanned by the display (log2(1000), about 9.97).
double filterOctaveSpan();
double filterXToHz(double x);
double filterHzToX(double hz);

enum class FilterPreset { Flat, LowPass, HighPass, BandPass };

struct FilterCurve {
    std::vector<float> db = std::vector<float>(kFilterCurvePoints, 0.0f);

    // dB at curve position x (0..1), interpolated; x is clamped to the ends.
    float sample(double x) const;

    // Classic shapes, with corners at 1 kHz: 24 dB/octave for low/high-pass,
    // 12 dB/octave either side for band-pass. Also gives the natural place
    // for the resonance point (the corner or centre).
    static FilterCurve preset(FilterPreset preset, double* resonanceX = nullptr);
};

// Knob-controlled settings that position the curve and its resonance.
struct FilterSettings {
    double shiftOctaves = 0.0; // cutoff: moves the curve right (up) by this much
    bool loop = false;         // wrap the curve around the ends when shifted
    double resonanceX = 0.5;   // resonance position, in curve coordinates
    double resonanceDb = 0.0;  // resonance level: the gain the filter has at that point
};

// Curve position (0..1) shown at display position x, or nothing if that part
// of the display is past the end of the curve (only possible without loop).
std::optional<double> displayToCurve(double displayX, const FilterSettings& settings);
// Where curve position x appears on the display, or nothing if shifted off it.
std::optional<double> curveToDisplay(double curveX, const FilterSettings& settings);
// Target gain (dB) at display position x: the shifted curve, with its end
// values extended (no loop) or wrapped (loop). Resonance not included.
float targetDb(const FilterCurve& curve, const FilterSettings& settings, double displayX);

// ---- Filter design -----------------------------------------------------------
//
// The filter is a cascade of 31 peaking ("bell") sections at 1/3-octave
// centres, like a graphic EQ, plus one narrow peaking section for resonance.
//
// Neighbouring bells overlap, so simply setting each band to the curve's
// value overshoots. Following Valimaki & Liski, "Accurate Cascade Graphic
// Equalizer" (IEEE SPL, 2017), the band gains come from a least-squares solve
// against a precomputed band-interaction matrix, then one correction pass
// using the real response.

class FilterDesigner {
public:
    static constexpr int kBands = 31;
    static constexpr int kSections = kBands + 1;        // + resonance
    static constexpr int kMatchPoints = 2 * kBands - 1; // centres + midpoints between them
    // Tuned with the "[.diag]" test: wider bells follow slopes better but go
    // unstable on drawn detail.
    static constexpr double kDefaultBandQ = 1.5;
    static constexpr double kResonanceQ = 8.6;          // about 1/6 octave wide

    using Sections = std::array<BiquadCoeffs, kSections>;

    explicit FilterDesigner(double sampleRate, double bandQ = kDefaultBandQ);

    double sampleRate() const { return sampleRate_; }
    double bandQ() const { return bandQ_; }

    // Design all sections for this curve and settings. Allocation-free, so
    // it can run on the audio thread.
    void design(const FilterCurve& curve, const FilterSettings& settings, Sections& out) const;

    // Total response of a designed filter at `frequencyHz`, in dB.
    double responseDb(const Sections& sections, double frequencyHz) const;

    static double bandCentreHz(int band);

private:
    double sampleRate_;
    double bandQ_;
    std::array<double, kMatchPoints> matchHz_{};
    // Least-squares solve matrix: band gains = solve_ * target gains.
    std::array<std::array<double, kMatchPoints>, kBands> solve_{};
};

// ---- Real-time filter --------------------------------------------------------

// Runs a designed filter over audio. Call update() once per block with the
// latest curve and settings; the new coefficients are blended in across the
// following block so moving the cutoff doesn't crackle.
class DrawnFilter {
public:
    explicit DrawnFilter(double sampleRate);

    // Redesigns only when the settings changed or curveChanged() was called.
    // Allocation-free.
    void update(const FilterCurve& curve, const FilterSettings& settings);
    // The curve's contents (or the curve object) changed: redesign next update.
    void curveChanged() { curveChanged_ = true; }
    void process(float* samples, int count);
    void reset();

    const FilterDesigner& designer() const { return designer_; }

private:
    FilterDesigner designer_;
    FilterDesigner::Sections current_{}, from_{}, to_{};
    std::array<Biquad, FilterDesigner::kSections> state_{};
    bool blending_ = false;

    // What the last design was made from, to skip redundant work.
    FilterSettings lastSettings_{};
    bool curveChanged_ = false;
    bool designed_ = false;
};

} // namespace hw
