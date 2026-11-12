#include "analysis/KeyEstimator.h"
#include "analysis/Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace {

// The log-frequency grid: 3 bins per semitone, MIDI 24 (C1) .. 108 (C8).
constexpr int kBinsPerSemi = 3;
constexpr int kLoMidi = 24;
constexpr int kHiMidi = 108;
constexpr int kLogBins = (kHiMidi - kLoMidi) * kBinsPerSemi + 1;
constexpr int kSemis = 84;            // semitone centres MIDI 24 .. 107
constexpr double kGamma = 100.0;      // log compression: log(1 + gamma * s / max)
constexpr int kWhiten = 12;           // bins (4 semitones) of the moving average taken off
constexpr int kBassLo = 28, kBassHi = 52;     // E1 .. Eb3
constexpr int kTrebleLo = 48, kTrebleHi = 96; // C3 .. B6

// Learned from labelled tracks (multinomial logistic regression over the 24
// keys, profiles shared by rotation): tonic first, then each semitone up.
constexpr std::array<double, 12> kMajorBass = {0.5119, 0.2113, 0.1633, -0.8585, 0.3261, 0.6000, -0.2059, 0.4935, -0.4554, -0.3980, -0.2146, -0.1788};
constexpr std::array<double, 12> kMajorTreble = {2.2943, -0.5884, 0.4240, -1.4622, 1.2114, -0.0243, -0.0998, 1.0084, -0.8158, -0.6137, -1.4154, 0.0765};
constexpr std::array<double, 12> kMinorBass = {0.6376, 0.3103, -0.6780, -0.0322, -0.5539, -0.2475, 0.0536, 0.2716, 0.4406, -0.2435, -0.1076, 0.1542};
constexpr std::array<double, 12> kMinorTreble = {2.1016, -0.1765, 0.5519, 0.5457, -0.7749, -0.6645, -0.5732, 1.2988, -0.9046, -1.1458, -0.0425, -0.2109};
constexpr double kMinorBias = 3.0682; // most of this music is minor
constexpr double kFeatureScale = 10.0; // the chroma (each summing to 1) as the model saw them

} // namespace

KeyResult estimateKey(const std::vector<float> &x, uint32_t sr)
{
    KeyResult res;
    if (sr == 0)
        return res;
    // A window of ~0.74 s (16384 at 22050 Hz): bass semitones need it.
    int n = 1;
    while (n < int(sr * 16384.0 / 22050.0 * 0.75))
        n <<= 1;
    const int hop = n / 4;
    if (x.size() < size_t(n) * 4)
        return res;
    const int bins = n / 2 + 1;
    const size_t frames = (x.size() - size_t(n)) / size_t(hop) + 1;

    // Each FFT bin feeds the (up to two) log bins next to it, by distance.
    std::vector<int> toBin(size_t(bins), -1);
    std::vector<float> toW(size_t(bins), 0.0f);
    for (int b = 1; b < bins; ++b) {
        const double midi = 69.0 + 12.0 * std::log2(double(b) * sr / n / 440.0);
        const double pos = (midi - kLoMidi) * kBinsPerSemi;
        const int lo = int(std::floor(pos));
        if (lo < -1 || lo >= kLogBins)
            continue;
        toBin[size_t(b)] = lo;
        toW[size_t(b)] = float(pos - lo); // share for lo + 1; 1 - share for lo
    }

    std::vector<float> win(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        win[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(n - 1));

    std::vector<float> spec(frames * kLogBins, 0.0f);
    std::vector<std::complex<float>> buf(static_cast<size_t>(n));
    float peak = 0.0f;
    for (size_t t = 0; t < frames; ++t) {
        const size_t off = t * size_t(hop);
        for (int i = 0; i < n; ++i)
            buf[size_t(i)] = {x[off + size_t(i)] * win[size_t(i)], 0.0f};
        fftInPlace(buf);
        float *row = &spec[t * kLogBins];
        for (int b = 1; b < bins; ++b) {
            const int lo = toBin[size_t(b)];
            if (lo < -1)
                continue;
            const float m = std::abs(buf[size_t(b)]);
            const float w = toW[size_t(b)];
            if (lo >= 0)
                row[lo] += m * (1.0f - w);
            if (lo + 1 < kLogBins)
                row[lo + 1] += m * w;
        }
        for (int i = 0; i < kLogBins; ++i)
            peak = std::max(peak, row[i]);
    }
    if (peak <= 0.0f)
        return res;

    // Tuning: the circular mean of where in the semitone (which third) the energy is.
    std::array<double, 3> third{};
    for (size_t t = 0; t < frames; ++t)
        for (int i = 0; i < kLogBins; ++i)
            third[size_t(i % 3)] += spec[t * kLogBins + size_t(i)];
    double re = 0, im = 0;
    for (int i = 0; i < 3; ++i) {
        re += third[size_t(i)] * std::cos(2.0 * M_PI * i / 3.0);
        im += third[size_t(i)] * std::sin(2.0 * M_PI * i / 3.0);
    }
    const double tune = std::atan2(im, re) / (2.0 * M_PI) * 3.0; // in log bins, -1.5 .. 1.5

    std::array<double, 12> bass{}, treble{};
    std::vector<double> lg(kLogBins), avg(kLogBins);
    std::array<double, kSemis> semi{};
    for (size_t t = 0; t < frames; ++t) {
        const float *row = &spec[t * kLogBins];
        for (int i = 0; i < kLogBins; ++i)
            lg[size_t(i)] = std::log1p(kGamma * row[i] / peak);
        // whitening: minus the average of the 12 bins round it (i - 6 .. i + 5,
        // mirrored at the ends), floored at 0
        for (int i = 0; i < kLogBins; ++i) {
            double s = 0.0;
            for (int j = i - kWhiten / 2; j < i + kWhiten / 2; ++j) {
                int k = j < 0 ? -j - 1 : j >= kLogBins ? 2 * kLogBins - j - 1 : j;
                s += lg[size_t(k)];
            }
            avg[size_t(i)] = s / kWhiten;
        }
        double total = 0.0;
        for (int s = 0; s < kSemis; ++s) {
            const double pos = s * kBinsPerSemi + tune;
            const int i0 = std::clamp(int(std::floor(pos)), 0, kLogBins - 2);
            const double fr = pos - std::floor(pos);
            const double a = std::max(0.0, lg[size_t(i0)] - avg[size_t(i0)]);
            const double b = std::max(0.0, lg[size_t(i0 + 1)] - avg[size_t(i0 + 1)]);
            semi[size_t(s)] = a * (1.0 - fr) + b * fr;
            total += semi[size_t(s)];
        }
        if (total <= 0.0)
            continue;
        // each frame counts equally, so a loud drop doesn't outvote the rest
        for (int s = 0; s < kSemis; ++s) {
            const int midi = kLoMidi + s;
            const double v = semi[size_t(s)] / total;
            if (midi >= kBassLo && midi < kBassHi)
                bass[size_t(midi % 12)] += v;
            if (midi >= kTrebleLo && midi < kTrebleHi)
                treble[size_t(midi % 12)] += v;
        }
    }
    double sb = 0, st = 0;
    for (int i = 0; i < 12; ++i) {
        sb += bass[size_t(i)];
        st += treble[size_t(i)];
    }
    if (sb <= 0.0 || st <= 0.0)
        return res;
    for (int i = 0; i < 12; ++i) {
        bass[size_t(i)] *= kFeatureScale / sb;
        treble[size_t(i)] *= kFeatureScale / st;
    }

    std::array<double, 24> score{};
    for (int k = 0; k < 24; ++k) {
        const bool minor = k >= 12;
        const auto &pb = minor ? kMinorBass : kMajorBass;
        const auto &pt = minor ? kMinorTreble : kMajorTreble;
        double z = minor ? kMinorBias : 0.0;
        for (int i = 0; i < 12; ++i) {
            const size_t pc = size_t((i + k) % 12);
            z += pb[size_t(i)] * bass[pc] + pt[size_t(i)] * treble[pc];
        }
        score[size_t(k)] = z;
    }
    const int best = int(std::max_element(score.begin(), score.end()) - score.begin());
    double sum = 0.0;
    for (double z : score)
        sum += std::exp(z - score[size_t(best)]);
    res.ok = true;
    res.key = best;
    res.confidence = 1.0 / sum;
    return res;
}
