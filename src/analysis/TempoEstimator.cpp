#include "analysis/TempoEstimator.h"
#include "analysis/Fft.h"
#include "core/BeatGrid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace {

constexpr int kFft = 1024;
constexpr int kHop = 256;

std::vector<float> detrend(const std::vector<float> &odf)
{
    // Remove the slowly varying part, keep positive peaks.
    const int half = 16;
    std::vector<double> csum(odf.size() + 1, 0.0);
    for (size_t i = 0; i < odf.size(); ++i)
        csum[i + 1] = csum[i] + odf[i];
    std::vector<float> out(odf.size());
    for (int i = 0; i < int(odf.size()); ++i) {
        const int a = std::max(0, i - half), b = std::min(int(odf.size()), i + half + 1);
        const double mean = (csum[size_t(b)] - csum[size_t(a)]) / double(b - a);
        out[size_t(i)] = std::max(0.0f, float(odf[size_t(i)] - mean));
    }
    return out;
}

// Bands for the beat-to-beat changes the downbeat is found from (Hz edges).
constexpr int kBands = 8;
constexpr double kBandEdges[kBands + 1] = {30, 90, 180, 350, 700, 1400, 2800, 5600, 11000};
using Bands = std::array<float, kBands>;

// The downbeat model (multinomial logistic regression over the four ways a
// bar can sit, 5-fold cross-validated at 90% on 105 edits cut at off-beat
// points): [beat of the bar, from the candidate 1][feature: level, rise,
// fall, biggest rise in the phrase][band].
constexpr double kDownbeatSure = 0.38; // its probability for the best way, else the first strong hit decides
constexpr double kDownbeatW[4][4][kBands] = {
    {{0.3529, 0.0899, -0.0218, -0.0543, -0.0930, -0.1329, -0.0995, -0.0506}, {0.3746, 0.1186, 0.0091, -0.0138, -0.0223, -0.0607, -0.0271, 0.0087}, {-0.1189, 0.0588, 0.1071, 0.1170, 0.1278, 0.1639, 0.1125, 0.0901}, {0.8040, 0.2652, 0.0113, -0.0261, 0.0079, -0.0515, 0.0152, 0.0962}},
    {{-0.0435, 0.0056, 0.0320, 0.0518, 0.0450, 0.0314, 0.0369, 0.0200}, {-0.3124, -0.1172, -0.0190, 0.0307, 0.0524, 0.0644, 0.0493, 0.0109}, {0.0879, -0.0274, -0.0661, -0.0678, -0.0797, -0.0958, -0.0842, -0.0582}, {-0.4711, -0.2214, -0.0427, 0.0431, 0.0433, 0.0591, 0.0323, -0.0413}},
    {{-0.1205, -0.0863, -0.0512, -0.0371, 0.0320, 0.0523, 0.0709, 0.0480}, {-0.0255, -0.0562, -0.0362, -0.0475, -0.0037, 0.0110, 0.0212, 0.0157}, {0.0227, 0.0083, 0.0195, 0.0119, -0.0214, -0.0406, -0.0440, -0.0410}, {-0.1727, -0.0936, -0.0216, -0.0602, 0.0066, 0.0219, 0.0203, -0.0048}},
    {{-0.1889, -0.0092, 0.0409, 0.0396, 0.0161, 0.0492, -0.0084, -0.0174}, {-0.0367, 0.0548, 0.0461, 0.0306, -0.0264, -0.0147, -0.0433, -0.0354}, {0.0083, -0.0397, -0.0605, -0.0610, -0.0267, -0.0275, 0.0157, 0.0091}, {-0.1602, 0.0499, 0.0529, 0.0433, -0.0578, -0.0295, -0.0679, -0.0500}},
};

// Full-band spectral flux, plus low-band (kick, < ~150 Hz) flux used to pick
// the on-beat phase over the off-beat hats; and each frame's log energy in kBands bands.
void onsetEnvelopes(const std::vector<float> &x, uint32_t sr, std::vector<float> &full, std::vector<float> &low,
                    std::vector<Bands> &bands)
{
    const int frames = x.size() > size_t(kFft) ? int((x.size() - kFft) / kHop) + 1 : 0;
    std::vector<float> odf(size_t(std::max(frames, 0)), 0.0f);
    std::vector<float> lowOdf(odf.size(), 0.0f);
    full.clear();
    low.clear();
    bands.assign(size_t(std::max(frames, 0)), Bands{});
    if (frames < 2)
        return;
    int bandOf[kFft / 2];
    for (int b = 0; b < kFft / 2; ++b) {
        const double f = double(b) * sr / kFft;
        bandOf[b] = -1;
        for (int k = 0; k < kBands; ++k)
            if (f >= kBandEdges[k] && f < kBandEdges[k + 1])
                bandOf[b] = k;
    }
    const int lowBins = std::max(2, int(150.0 * kFft / double(sr)));

    std::vector<float> win(kFft);
    for (int i = 0; i < kFft; ++i)
        win[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(kFft));

    const int bins = kFft / 2;
    std::vector<float> prev(size_t(bins), 0.0f), cur(size_t(bins), 0.0f);
    std::vector<std::complex<float>> buf(kFft);
    for (int t = 0; t < frames; ++t) {
        const size_t off = size_t(t) * kHop;
        for (int i = 0; i < kFft; ++i)
            buf[size_t(i)] = {x[off + size_t(i)] * win[size_t(i)], 0.0f};
        fftInPlace(buf);
        float flux = 0.0f, lowFlux = 0.0f;
        Bands e{};
        for (int b = 1; b < bins; ++b) {
            const float mag = std::abs(buf[size_t(b)]);
            if (bandOf[b] >= 0)
                e[size_t(bandOf[b])] += mag * mag;
            cur[size_t(b)] = std::log1p(100.0f * mag);
            const float d = cur[size_t(b)] - prev[size_t(b)];
            if (d > 0) {
                flux += d;
                if (b <= lowBins)
                    lowFlux += d;
            }
        }
        for (int k = 0; k < kBands; ++k)
            bands[size_t(t)][size_t(k)] = std::log10(1e-6f + e[size_t(k)]);
        odf[size_t(t)] = t == 0 ? 0.0f : flux;
        lowOdf[size_t(t)] = t == 0 ? 0.0f : lowFlux;
        std::swap(prev, cur);
    }
    full = detrend(odf);
    low = detrend(lowOdf);
}

double interp(const std::vector<double> &v, double idx)
{
    if (idx < 0 || idx >= double(v.size() - 1))
        return 0.0;
    const size_t i = size_t(idx);
    const double f = idx - double(i);
    return v[i] * (1.0 - f) + v[i + 1] * f;
}

// Fold the envelope at period P (frames). Returns peak strength and the phase (frames).
struct Fold {
    double strength = 0.0;
    double phase = 0.0;
};

Fold foldAt(const std::vector<float> &odf, double period, int B = 48)
{
    std::vector<double> hist(size_t(B), 0.0);
    for (size_t t = 0; t < odf.size(); ++t) {
        if (odf[t] <= 0.0f)
            continue;
        const double ph = std::fmod(double(t), period) / period * B;
        const int b0 = int(ph) % B;
        const double f = ph - std::floor(ph);
        hist[size_t(b0)] += odf[t] * (1.0 - f);
        hist[size_t((b0 + 1) % B)] += odf[t] * f;
    }
    double mean = 0.0;
    for (double h : hist)
        mean += h;
    mean /= B;
    Fold best;
    auto at = [&](int b) { return hist[size_t(((b % B) + B) % B)]; };
    for (int b = 0; b < B; ++b) {
        const double s = at(b - 1) * 0.5 + at(b) + at(b + 1) * 0.5;
        if (s > best.strength) {
            best.strength = s;
            // centroid of the three bins for sub-bin phase
            const double w0 = at(b - 1), w1 = at(b), w2 = at(b + 1);
            const double c = (w2 - w0) / std::max(1e-9, w0 + w1 + w2);
            best.phase = std::fmod((b + c + B) / B * period, period);
        }
    }
    best.strength = mean > 0 ? best.strength / (2.0 * mean) : 0.0;
    return best;
}

// ---- fine timing ----------------------------------------------------------
// The STFT envelopes above find the tempo well but place beats ~15-60 ms
// late on real kicks (46 ms windows, softer attacks than the calibration
// clicks). The beat phase is refined on a 1.5 ms envelope instead.
constexpr int kFineHop = 32;   // 1.45 ms at 22.05 kHz
constexpr double kPulseEdge = 0.7; // the beat: where the folded pulse first reaches this much of its peak
constexpr int kFineWin = 128;  // 5.8 ms

// Energy rise envelope at kFineHop: positive change of log energy over a few
// frames. low = kick band (two one-pole low-passes at ~150 Hz), else full band.
std::vector<float> fineRise(const std::vector<float> &x, uint32_t sr, bool low, std::vector<float> *energyOut = nullptr)
{
    std::vector<float> y(x);
    if (low) {
        const float a = float(1.0 - std::exp(-2.0 * M_PI * 150.0 / sr));
        for (int pass = 0; pass < 2; ++pass) {
            float z = 0.0f;
            for (float &v : y) {
                z += a * (v - z);
                v = z;
            }
        }
    }
    const size_t frames = y.size() > size_t(kFineWin) ? (y.size() - kFineWin) / kFineHop + 1 : 0;
    std::vector<float> e(frames, 0.0f);
    for (size_t t = 0; t < frames; ++t) {
        double s = 0.0;
        const float *p = y.data() + t * kFineHop;
        for (int i = 0; i < kFineWin; ++i)
            s += double(p[i]) * p[i];
        e[t] = float(s / kFineWin);
    }
    if (energyOut)
        *energyOut = e;
    std::vector<float> rise(frames, 0.0f);
    constexpr int lag = 3;
    for (size_t t = lag; t < frames; ++t) {
        const float d = std::log(1e-9f + e[t]) - std::log(1e-9f + e[t - lag]);
        rise[t] = std::max(0.0f, d);
    }
    return rise;
}

// Frame index of the fine envelope -> seconds at the onset (calibrated on the
// synthetic kicks in engine_test: the rise peaks as the attack fills the window).
double fineFrameToSec(double t, uint32_t sr) { return (t * kFineHop + 0.5 * kFineWin) / double(sr); }

} // namespace

TempoResult estimateTempo(const std::vector<float> &mono, uint32_t sr, double minBpm, double maxBpm)
{
    TempoResult res;
    std::vector<float> odf, lowOdf;
    std::vector<Bands> bands;
    onsetEnvelopes(mono, sr, odf, lowOdf, bands);
    if (odf.size() < 400)
        return res;
    const double fps = double(sr) / kHop;

    // Autocorrelations (whole band and kick band) out to 16 beats of the
    // slowest tempo: a real beat repeats in bars and phrases, a dotted or
    // triplet pulse (DnB's two-step heard at 2/3) doesn't.
    const int maxLag = int(std::ceil(fps * 60.0 / minBpm * 16.0)) + 2;
    const size_t N = odf.size();
    auto autocorr = [&](const std::vector<float> &o) {
        std::vector<double> ac(size_t(maxLag) + 1, 0.0);
        for (int L = 0; L <= maxLag && size_t(L) < N; ++L) {
            double s = 0.0;
            for (size_t t = 0; t + size_t(L) < N; ++t)
                s += double(o[t]) * o[t + size_t(L)];
            ac[size_t(L)] = s / double(N - size_t(L));
        }
        const double a0 = ac[0] > 0.0 ? ac[0] : 1.0;
        for (double &v : ac)
            v /= a0;
        return ac;
    };
    const std::vector<double> ac = autocorr(odf), acLow = autocorr(lowOdf);

    // Coarse tempo: repetition at 1, 2, 4, 8 and 16 beats, a soft prior around 140 BPM.
    double bestBpm = 0.0, bestScore = -1.0;
    for (double bpm = minBpm; bpm <= maxBpm; bpm += 0.1) {
        const double lag = fps * 60.0 / bpm;
        double s = 0.0;
        for (auto [k, w] : {std::pair{1.0, 1.0}, {2.0, 0.5}, {4.0, 0.5}, {8.0, 0.5}, {16.0, 0.5}})
            s += w * 0.5 * (interp(ac, k * lag) + interp(acLow, k * lag));
        const double z = std::log2(bpm / 140.0);
        const double score = s * std::exp(-0.5 * z * z);
        if (score > bestScore) {
            bestScore = score;
            bestBpm = bpm;
        }
    }
    if (bestBpm <= 0.0)
        return res;

    // Fine tempo: fold the whole track, the sharpest fold wins.
    auto foldBpm = [&](double bpm) { return foldAt(odf, fps * 60.0 / bpm); };
    double fineBpm = bestBpm;
    Fold fineFold = foldBpm(bestBpm);
    for (double bpm = bestBpm - 0.6; bpm <= bestBpm + 0.6; bpm += 0.01) {
        const Fold f = foldBpm(bpm);
        if (f.strength > fineFold.strength) {
            fineFold = f;
            fineBpm = bpm;
        }
    }
    // Electronic tracks are almost always produced at integer (or .5) tempos.
    for (double snap : {std::round(fineBpm), std::round(fineBpm * 2.0) / 2.0}) {
        if (std::abs(snap - fineBpm) < 0.08) {
            const Fold f = foldBpm(snap);
            if (f.strength >= fineFold.strength * 0.92) {
                fineBpm = snap;
                fineFold = f;
                break;
            }
        }
    }

    // Frame index -> seconds. With log compression the flux peaks as soon as the
    // onset enters the tail of the window, ~80% into the frame (calibrated on
    // synthetic kicks).
    auto frameToSec = [&](double t) { return (t * kHop + 0.795 * kFft) / double(sr); };

    const double P = fps * 60.0 / fineBpm;
    const double spb = 60.0 / fineBpm;
    // Prefer the kick phase when there is a clear low-end pulse.
    const Fold lowFold = foldAt(lowOdf, P);
    const bool kick = lowFold.strength > 1.5;
    const double coarseSec = std::fmod(frameToSec(kick ? lowFold.phase : fineFold.phase), spb);

    // Refine the phase on the fine envelope, near the coarse one (so it can't
    // hop to the off-beat hats), and early: the coarse one runs late.
    std::vector<float> energy;
    const std::vector<float> rise = fineRise(mono, sr, kick, &energy);
    double phaseSec = coarseSec;
    if (!rise.empty()) {
        const double fineP = double(sr) / kFineHop * spb;
        constexpr int B = 256;
        std::vector<double> hist(B, 0.0);
        for (size_t t = 0; t < rise.size(); ++t) {
            if (rise[t] <= 0.0f)
                continue;
            hist[size_t(std::fmod(double(t), fineP) / fineP * B) % B] += rise[t];
        }
        auto smooth = [&](int b) {
            double s = 0.0;
            for (int k = -2; k <= 2; ++k)
                s += hist[size_t(((b + k) % B + B) % B)] * (3 - std::abs(k));
            return s;
        };
        auto secAt = [&](double b) { return std::fmod(fineFrameToSec(b / B * fineP, sr) + spb, spb); };
        double best = -1.0;
        int bestBin = 0;
        for (int b = 0; b < B; ++b) {
            const double d = wrapCentered(secAt(b) - coarseSec, spb);   // + = later than coarse
            if (d < -0.090 || d > 0.030)
                continue;
            const double s = smooth(b);
            if (s > best) {
                best = s;
                bestBin = b;
            }
        }
        // The beat is where the pulse starts, not its steepest point: a real
        // kick builds up over several ms. Back from the peak to where the
        // folded pulse first reaches kPulseEdge of it (at most 40 ms back).
        double edge = bestBin;
        const int maxBack = int(0.040 / spb * B);
        for (int k = 1; k <= maxBack; ++k) {
            const double s = smooth(bestBin - k);
            if (s < kPulseEdge * best) {
                const double s1 = smooth(bestBin - k + 1);
                edge = bestBin - k + (kPulseEdge * best - s) / std::max(1e-12, s1 - s);
                break;
            }
            edge = bestBin - k;
        }
        phaseSec = secAt(edge);
    }

    // Where the music starts: the first fine frame with real energy. MP3s
    // often begin with a few ms of encoder padding (digital silence).
    double startSec = 0.0;
    bool hardStart = false;
    if (!energy.empty()) {
        std::vector<float> sortedE(energy);
        std::nth_element(sortedE.begin(), sortedE.begin() + long(sortedE.size() * 9 / 10), sortedE.end());
        const float loud = sortedE[sortedE.size() * 9 / 10];
        size_t t0 = 0;
        while (t0 < energy.size() && energy[t0] < loud * 1e-4f)
            ++t0;
        // the exact first sample, 40 dB under the loud level
        const float amp = 0.01f * std::sqrt(loud);
        size_t i = t0 * kFineHop;
        while (i < mono.size() && i < (t0 + 1) * kFineHop + kFineWin && std::abs(mono[i]) < amp)
            ++i;
        startSec = double(i) / sr;
        // A hit, not a fade-in: loud within 40 ms of the first sound.
        const size_t t1 = std::min(energy.size(), t0 + size_t(0.040 * sr / kFineHop));
        for (size_t t = t0; t < t1; ++t)
            hardStart |= energy[t] >= 0.1f * loud;
    }

    // Which beat is the 1. Each beat's energy in kBands bands, and how it
    // changed from the beat before, averaged per place in the bar (and the
    // biggest rises per place in the 4-bar phrase); a small model learned on
    // my edits (where the 1 is known) scores the four ways the bar can sit.
    // The 1 carries the most low end, and the low end comes in on it,
    // phrases most of all; the 2 is where it falls away.
    int barPhase = -1; // grid beats after the one at phaseSec that are a 1 (mod 4)
    {
        const int beatsN = int((double(mono.size()) / sr - phaseSec) / spb);
        std::vector<Bands> beat(size_t(std::max(0, beatsN)), Bands{});
        std::vector<int> cnt(beat.size(), 0);
        for (size_t t = 0; t < bands.size(); ++t) {
            const double sec = double(t) * kHop / sr; // where the frame starts: it holds only that beat's sound on
            const int i = int(std::floor((sec - phaseSec) / spb));
            if (i < 0 || i >= beatsN)
                continue;
            for (int k = 0; k < kBands; ++k)
                beat[size_t(i)][size_t(k)] += bands[t][size_t(k)];
            ++cnt[size_t(i)];
        }
        if (beatsN >= 32) {
            Bands mean{};
            for (size_t i = 0; i < beat.size(); ++i)
                for (int k = 0; k < kBands; ++k) {
                    beat[i][size_t(k)] = cnt[i] ? beat[i][size_t(k)] / float(cnt[i]) : 0.0f;
                    mean[size_t(k)] += beat[i][size_t(k)] / float(beat.size());
                }
            // per place in the bar: level, rise, fall; per place in the phrase: rise
            double feat[4][4][kBands] = {};
            int perBar[4] = {};
            double rise16[16][kBands] = {};
            int per16[16] = {};
            for (size_t i = 0; i < beat.size(); ++i) {
                const int p = int(i % 4), q = int(i % 16);
                ++perBar[p];
                ++per16[q];
                for (int k = 0; k < kBands; ++k) {
                    const double d = i ? double(beat[i][size_t(k)]) - beat[i - 1][size_t(k)] : 0.0;
                    feat[p][0][k] += beat[i][size_t(k)] - mean[size_t(k)];
                    feat[p][1][k] += std::max(0.0, d);
                    feat[p][2][k] += std::max(0.0, -d);
                    rise16[q][k] += std::max(0.0, d);
                }
            }
            for (int p = 0; p < 4; ++p)
                for (int k = 0; k < kBands; ++k) {
                    for (int f = 0; f < 3; ++f)
                        feat[p][f][k] /= std::max(1, perBar[p]);
                    feat[p][3][k] = 0.0;
                    for (int q = p; q < 16; q += 4)
                        feat[p][3][k] = std::max(feat[p][3][k], rise16[q][k] / std::max(1, per16[q]));
                }
            double z[4] = {};
            for (int p = 0; p < 4; ++p)
                for (int j = 0; j < 4; ++j)
                    for (int f = 0; f < 4; ++f)
                        for (int k = 0; k < kBands; ++k)
                            z[p] += kDownbeatW[j][f][k] * feat[(p + j) % 4][f][k];
            const int p = int(std::max_element(z, z + 4) - z);
            double sum = 0.0;
            for (double v : z)
                sum += std::exp(v - z[p]);
            // Sure enough? Bars that all sound alike leave it guessing (a
            // quarter each); then the first strong hit below decides.
            if (1.0 / sum >= kDownbeatSure)
                barPhase = p;
        }
    }

    // Most tracks (and every edit exported from a DAW) start on a downbeat:
    // when the music starts on the grid, the grid starts there. A hard hit
    // may sit a little further off the refined phase than a soft start.
    double firstBeat;
    const double startOff = wrapCentered(startSec - phaseSec, spb);
    if (std::abs(startOff) < (hardStart ? 0.035 : 0.020)) {
        firstBeat = startSec;
    } else if (barPhase >= 0) {
        // the first downbeat at or after the start
        const double any = phaseSec + barPhase * spb, bar = 4.0 * spb;
        firstBeat = any - std::floor(any / bar) * bar;
    } else {
        // Too short or too even to tell: the first grid beat at/after the first strong onset.
        std::vector<float> sorted(odf);
        std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() * 95 / 100), sorted.end());
        const float strong = 0.3f * sorted[sorted.size() * 95 / 100];
        size_t firstOnset = 0;
        while (firstOnset < N && odf[firstOnset] < strong)
            ++firstOnset;
        firstBeat = phaseSec;
        while (firstBeat < frameToSec(double(firstOnset)) - 0.25 * spb)
            firstBeat += spb;
    }

    res.ok = true;
    res.bpm = fineBpm;
    res.firstBeatSec = std::max(0.0, firstBeat);
    res.confidence = std::clamp((fineFold.strength - 1.0) / 3.0, 0.0, 1.0);
    return res;
}
