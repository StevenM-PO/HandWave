#include "DrawnFilter.h"

#include <algorithm>
#include <cmath>

namespace hw {

namespace {

// Bell gain used to measure how the bands interact (from the paper).
constexpr double kPrototypeGainDb = 17.0;
// Limits on the solved band gains: beyond these the bells stop behaving.
constexpr double kMinBandDb = -60.0;
constexpr double kMaxBandDb = 30.0;
// Samples per coefficient step while blending to a new design.
constexpr int kBlendStep = 32;

double wrap01(double x)
{
    return x - std::floor(x);
}

bool sameSettings(const FilterSettings& a, const FilterSettings& b)
{
    return a.shiftOctaves == b.shiftOctaves && a.loop == b.loop && a.resonanceX == b.resonanceX
        && a.resonanceDb == b.resonanceDb;
}

// In-place inverse of an n x n matrix by Gauss-Jordan elimination with
// partial pivoting. Only used once, at construction.
template <size_t N>
void invert(std::array<std::array<double, N>, N>& m)
{
    std::array<std::array<double, N>, N> inv{};
    for (size_t i = 0; i < N; ++i)
        inv[i][i] = 1.0;
    for (size_t col = 0; col < N; ++col) {
        size_t pivot = col;
        for (size_t r = col + 1; r < N; ++r)
            if (std::fabs(m[r][col]) > std::fabs(m[pivot][col]))
                pivot = r;
        std::swap(m[col], m[pivot]);
        std::swap(inv[col], inv[pivot]);
        const double scale = 1.0 / m[col][col];
        for (size_t c = 0; c < N; ++c) {
            m[col][c] *= scale;
            inv[col][c] *= scale;
        }
        for (size_t r = 0; r < N; ++r) {
            if (r == col)
                continue;
            const double factor = m[r][col];
            for (size_t c = 0; c < N; ++c) {
                m[r][c] -= factor * m[col][c];
                inv[r][c] -= factor * inv[col][c];
            }
        }
    }
    m = inv;
}

} // namespace

// ---- Curve --------------------------------------------------------------------

double filterOctaveSpan()
{
    static const double span = std::log2(kFilterMaxHz / kFilterMinHz);
    return span;
}

double filterXToHz(double x)
{
    return kFilterMinHz * std::exp2(x * filterOctaveSpan());
}

double filterHzToX(double hz)
{
    return std::log2(hz / kFilterMinHz) / filterOctaveSpan();
}

float FilterCurve::sample(double x) const
{
    const double pos = std::clamp(x, 0.0, 1.0) * (db.size() - 1);
    const size_t i = std::min(static_cast<size_t>(pos), db.size() - 2);
    const double frac = pos - static_cast<double>(i);
    return static_cast<float>(db[i] + (db[i + 1] - db[i]) * frac);
}

FilterCurve FilterCurve::preset(FilterPreset preset, double* resonanceX)
{
    constexpr double kCornerHz = 1000.0;
    FilterCurve curve;
    for (int i = 0; i < kFilterCurvePoints; ++i) {
        const double ratio = filterXToHz(static_cast<double>(i) / (kFilterCurvePoints - 1)) / kCornerHz;
        double db = 0.0;
        switch (preset) {
        case FilterPreset::Flat:
            break;
        case FilterPreset::LowPass: // 4th-order Butterworth shape
            db = -10.0 * std::log10(1.0 + std::pow(ratio, 8.0));
            break;
        case FilterPreset::HighPass:
            db = -10.0 * std::log10(1.0 + std::pow(1.0 / ratio, 8.0));
            break;
        case FilterPreset::BandPass: // 2nd-order slopes, peak normalized to 0 dB
            db = -10.0 * std::log10(1.0 + std::pow(ratio, 4.0))
                - 10.0 * std::log10(1.0 + std::pow(1.0 / ratio, 4.0))
                + 20.0 * std::log10(2.0);
            break;
        }
        curve.db[i] = static_cast<float>(std::clamp(db, double(kFilterMinDb), double(kFilterMaxDb)));
    }
    if (resonanceX)
        *resonanceX = filterHzToX(kCornerHz);
    return curve;
}

std::optional<double> displayToCurve(double displayX, const FilterSettings& settings)
{
    const double x = displayX - settings.shiftOctaves / filterOctaveSpan();
    if (settings.loop)
        return wrap01(x);
    if (x < 0.0 || x > 1.0)
        return std::nullopt;
    return x;
}

std::optional<double> curveToDisplay(double curveX, const FilterSettings& settings)
{
    const double x = curveX + settings.shiftOctaves / filterOctaveSpan();
    if (settings.loop)
        return wrap01(x);
    if (x < 0.0 || x > 1.0)
        return std::nullopt;
    return x;
}

float targetDb(const FilterCurve& curve, const FilterSettings& settings, double displayX)
{
    const double x = displayX - settings.shiftOctaves / filterOctaveSpan();
    return curve.sample(settings.loop ? wrap01(x) : x); // sample() clamps = extends the ends
}

// ---- Design -------------------------------------------------------------------

double FilterDesigner::bandCentreHz(int band)
{
    // ISO 1/3-octave series: 1 kHz is band 17; band 0 is ~20 Hz, band 30 ~20 kHz.
    return 1000.0 * std::exp2((band - 17) / 3.0);
}

FilterDesigner::FilterDesigner(double sampleRate, double bandQ)
    : sampleRate_(sampleRate)
    , bandQ_(bandQ)
{
    // Match the response at every band centre and halfway (geometrically) between them.
    for (int j = 0; j < kMatchPoints; ++j) {
        const int band = j / 2;
        matchHz_[j] = (j % 2 == 0) ? bandCentreHz(band)
                                   : std::sqrt(bandCentreHz(band) * bandCentreHz(band + 1));
    }

    // Interaction matrix: how much each band (at the prototype gain) moves
    // the response at each match point, per dB of band gain.
    std::array<std::array<double, kBands>, kMatchPoints> b{};
    for (int k = 0; k < kBands; ++k) {
        const BiquadCoeffs bell = peakingEq(bandCentreHz(k), bandQ_, kPrototypeGainDb, sampleRate_);
        for (int j = 0; j < kMatchPoints; ++j)
            b[j][k] = magnitudeDb(bell, matchHz_[j], sampleRate_) / kPrototypeGainDb;
    }

    // Least squares: solve = (B^T B)^-1 B^T.
    std::array<std::array<double, kBands>, kBands> btb{};
    for (int r = 0; r < kBands; ++r)
        for (int c = 0; c < kBands; ++c)
            for (int j = 0; j < kMatchPoints; ++j)
                btb[r][c] += b[j][r] * b[j][c];
    invert(btb);
    for (int r = 0; r < kBands; ++r)
        for (int j = 0; j < kMatchPoints; ++j) {
            double sum = 0.0;
            for (int c = 0; c < kBands; ++c)
                sum += btb[r][c] * b[j][c];
            solve_[r][j] = sum;
        }
}

void FilterDesigner::design(const FilterCurve& curve, const FilterSettings& settings, Sections& out) const
{
    std::array<double, kMatchPoints> target{};
    for (int j = 0; j < kMatchPoints; ++j)
        target[j] = targetDb(curve, settings, filterHzToX(matchHz_[j]));

    // Solve band gains, then correct once using the response they really give.
    std::array<double, kBands> gains{};
    std::array<double, kMatchPoints> error = target;
    for (int pass = 0; pass < 2; ++pass) {
        for (int k = 0; k < kBands; ++k) {
            double delta = 0.0;
            for (int j = 0; j < kMatchPoints; ++j)
                delta += solve_[k][j] * error[j];
            gains[k] = std::clamp(gains[k] + delta, kMinBandDb, kMaxBandDb);
            out[k] = peakingEq(bandCentreHz(k), bandQ_, gains[k], sampleRate_);
        }
        if (pass == 0) {
            for (int j = 0; j < kMatchPoints; ++j) {
                double actual = 0.0;
                for (int k = 0; k < kBands; ++k)
                    actual += magnitudeDb(out[k], matchHz_[j], sampleRate_);
                error[j] = target[j] - actual;
            }
        }
    }

    // Resonance: a narrow bell that lifts (or dips) the response at its point
    // to exactly the requested level. Shifted off the display = no resonance.
    BiquadCoeffs& resonance = out[kBands];
    resonance = BiquadCoeffs{};
    if (const auto x = curveToDisplay(settings.resonanceX, settings)) {
        const double hz = filterXToHz(*x);
        double achieved = 0.0;
        for (int k = 0; k < kBands; ++k)
            achieved += magnitudeDb(out[k], hz, sampleRate_);
        const double gain = std::clamp(settings.resonanceDb - achieved, kMinBandDb, 36.0);
        if (std::fabs(gain) > 0.05)
            resonance = peakingEq(hz, kResonanceQ, gain, sampleRate_);
    }
}

double FilterDesigner::responseDb(const Sections& sections, double frequencyHz) const
{
    double db = 0.0;
    for (const BiquadCoeffs& s : sections)
        db += magnitudeDb(s, frequencyHz, sampleRate_);
    return db;
}

// ---- Real-time filter ---------------------------------------------------------

DrawnFilter::DrawnFilter(double sampleRate)
    : designer_(sampleRate)
{
}

void DrawnFilter::update(const FilterCurve& curve, const FilterSettings& settings)
{
    if (designed_ && !curveChanged_ && sameSettings(settings, lastSettings_))
        return;

    designer_.design(curve, settings, to_);
    if (designed_) {
        from_ = current_;
        blending_ = true;
    } else {
        current_ = to_;
        designed_ = true;
    }
    lastSettings_ = settings;
    curveChanged_ = false;
}

void DrawnFilter::process(float* samples, int count)
{
    const int steps = std::max(1, (count + kBlendStep - 1) / kBlendStep);
    for (int step = 0; step < steps; ++step) {
        if (blending_) {
            const double t = static_cast<double>(step + 1) / steps;
            for (int s = 0; s < FilterDesigner::kSections; ++s)
                current_[s] = BiquadCoeffs::lerp(from_[s], to_[s], t);
        }
        const int begin = step * kBlendStep;
        const int end = std::min(count, begin + kBlendStep);
        for (int i = begin; i < end; ++i) {
            double x = samples[i];
            for (int s = 0; s < FilterDesigner::kSections; ++s)
                x = state_[s].process(current_[s], x);
            samples[i] = static_cast<float>(x);
        }
    }
    if (blending_) {
        current_ = to_;
        blending_ = false;
    }
}

void DrawnFilter::reset()
{
    for (Biquad& b : state_)
        b.reset();
}

} // namespace hw
