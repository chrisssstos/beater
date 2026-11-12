#include "analysis/StructureEstimator.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kLowHz = 120.0;     // the kick and the bass
constexpr double kDropBelowDb = 7.0; // a bar this far under the drop level isn't a drop
constexpr double kDropPercentile = 0.85;
constexpr int kMinRunBars = 4;       // shorter runs join their neighbours
constexpr int kPhraseBars = 4;

// Second-order Butterworth low-pass (RBJ cookbook), run in place.
void lowPass(std::vector<float> &x, double fc, double sr)
{
    const double w = 2.0 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0 * M_SQRT1_2);
    const double a0 = 1.0 + al;
    const double b0 = (1.0 - c) / 2.0 / a0, b1 = (1.0 - c) / a0, b2 = b0;
    const double a1 = -2.0 * c / a0, a2 = (1.0 - al) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float &v : x) {
        const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = v;
        y2 = y1;
        y1 = y;
        v = float(y);
    }
}

} // namespace

StructureResult estimateStructure(const std::vector<float> &x, uint32_t sr, double bpm, double firstBeatSec)
{
    StructureResult res;
    if (sr == 0 || bpm <= 0.0 || x.empty())
        return res;
    const double barSec = 4.0 * 60.0 / bpm;
    const double dur = double(x.size()) / sr;
    // bars from the first downbeat at or after the start, whole bars only
    double first = firstBeatSec - std::floor(firstBeatSec / barSec) * barSec;
    const int nBars = int(std::floor((dur - first) / barSec));
    if (nBars < 8)
        return res;

    std::vector<float> low(x);
    lowPass(low, kLowHz, sr);
    lowPass(low, kLowHz, sr); // 4th order: hats and snares out

    std::vector<double> db(static_cast<size_t>(nBars));
    for (int k = 0; k < nBars; ++k) {
        const size_t a = size_t((first + k * barSec) * sr), b = std::min(x.size(), size_t((first + (k + 1) * barSec) * sr));
        double e = 0.0;
        for (size_t i = a; i < b; ++i)
            e += double(low[i]) * low[i];
        db[size_t(k)] = 10.0 * std::log10(e / std::max<size_t>(1, b - a) + 1e-12);
    }
    std::vector<double> sorted(db);
    std::sort(sorted.begin(), sorted.end());
    const double ref = sorted[size_t(kDropPercentile * (nBars - 1))];

    std::vector<int> drop(static_cast<size_t>(nBars));
    for (int k = 0; k < nBars; ++k)
        drop[size_t(k)] = db[size_t(k)] > ref - kDropBelowDb;
    // one odd bar (a fill, a gap before the drop) doesn't make a section
    std::vector<int> d = drop;
    for (int k = 1; k + 1 < nBars; ++k)
        if (drop[size_t(k - 1)] == drop[size_t(k + 1)])
            d[size_t(k)] = drop[size_t(k - 1)];

    // runs of the same kind; a short one joins the one before it
    struct Run { int a, b; bool drop; };
    std::vector<Run> runs;
    for (int k = 0; k < nBars; ++k) {
        if (runs.empty() || runs.back().drop != bool(d[size_t(k)]))
            runs.push_back({k, k + 1, bool(d[size_t(k)])});
        else
            runs.back().b = k + 1;
    }
    for (bool merged = true; merged && runs.size() > 1;) {
        merged = false;
        for (size_t i = 0; i < runs.size(); ++i) {
            if (runs[i].b - runs[i].a >= kMinRunBars)
                continue;
            if (i == 0) {
                runs[1].a = runs[0].a;
                runs.erase(runs.begin());
            } else {
                runs[i - 1].b = runs[i].b;
                runs.erase(runs.begin() + long(i));
            }
            // neighbours of the same kind become one
            for (size_t j = 1; j < runs.size();) {
                if (runs[j].drop == runs[j - 1].drop) {
                    runs[j - 1].b = runs[j].b;
                    runs.erase(runs.begin() + long(j));
                } else {
                    ++j;
                }
            }
            merged = true;
            break;
        }
    }
    // boundaries on the phrase when they're a bar off it
    for (size_t i = 1; i < runs.size(); ++i) {
        const int k = runs[i].a;
        const int p = int(std::lround(double(k) / kPhraseBars)) * kPhraseBars;
        if (std::abs(p - k) <= 1 && p > runs[i - 1].a && p < runs[i].b) {
            runs[i - 1].b = p;
            runs[i].a = p;
        }
    }

    for (const Run &r : runs)
        res.sections.push_back({first + r.a * barSec, first + r.b * barSec, r.drop});
    // The first drop: the first run of 16 bars or more (a short kick-in early
    // on is still the intro), else of 8 or more, else the first.
    for (int minBars : {16, 8, 1}) {
        for (const Run &r : runs) {
            if (r.drop && r.b - r.a >= minBars) {
                res.firstDropSec = first + r.a * barSec;
                break;
            }
        }
        if (res.firstDropSec >= 0.0)
            break;
    }
    for (const Run &r : runs)
        res.bars += std::string(size_t(r.b - r.a), r.drop ? 'D' : '.');
    res.ok = true;
    return res;
}
