// Headless checks for the audio core: tempo estimate, sync/phase lock,
// key shift, end loops, key estimate, sections, scheduled starts. Run: ./engine_test
#include "analysis/KeyEstimator.h"
#include "analysis/StructureEstimator.h"
#include "analysis/TempoEstimator.h"
#include "core/Key.h"
#include "audio/AudioEngine.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            ++g_fail;                                       \
            std::printf("  FAIL: " __VA_ARGS__);            \
            std::printf("\n");                              \
        }                                                   \
    } while (0)

// Kick on every beat, hat on the off-beat, silence before firstBeat.
static std::vector<float> clickTrack(double bpm, double firstBeat, double seconds, uint32_t sr, unsigned seed = 1)
{
    std::vector<float> x(size_t(seconds * sr), 0.0f);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    const double spb = 60.0 / bpm;
    for (double t = firstBeat; t < seconds; t += spb) {
        const size_t s = size_t(t * sr);
        for (size_t i = 0; i < size_t(0.12 * sr) && s + i < x.size(); ++i) {
            const double tt = double(i) / sr;
            x[s + i] += float(0.9 * std::sin(2 * M_PI * 55.0 * tt * (1.0 + 2.0 * std::exp(-tt * 40))) * std::exp(-tt * 18));
        }
        const size_t h = size_t((t + spb / 2) * sr);
        for (size_t i = 0; i < size_t(0.03 * sr) && h + i < x.size(); ++i)
            x[h + i] += 0.25f * u(rng) * float(std::exp(-double(i) / sr * 120));
    }
    return x;
}

static void testTempo()
{
    std::printf("tempo estimate\n");
    struct Case { double bpm, first; } cases[] = {{128.0, 0.5}, {124.0, 1.234}, {140.0, 0.1}, {174.0, 0.75}, {122.5, 0.3}};
    for (auto c : cases) {
        auto x = clickTrack(c.bpm, c.first, 90.0, 22050);
        TempoResult r = estimateTempo(x, 22050);
        std::printf("  %.2f bpm @ %.3fs -> %.3f bpm @ %.4fs (conf %.2f)\n", c.bpm, c.first, r.bpm, r.firstBeatSec, r.confidence);
        CHECK(r.ok && std::abs(r.bpm - c.bpm) < 0.02, "bpm %.3f vs %.3f", r.bpm, c.bpm);
        CHECK(std::abs(r.firstBeatSec - c.first) < 0.003, "first beat %.4f vs %.4f", r.firstBeatSec, c.first);
    }
    // A soft pad from 0:00 and the kicks from 2.123 s: the start isn't on the
    // grid, so the phase comes from the fine envelope, not the first sound.
    {
        auto x = clickTrack(128.0, 2.123, 90.0, 22050);
        for (size_t i = 0; i < size_t(4.0 * 22050); ++i) {
            const double t = double(i) / 22050;
            x[i] += float(0.15 * std::min(1.0, t / 1.5) * std::sin(2 * M_PI * 220.0 * t));
        }
        TempoResult r = estimateTempo(x, 22050);
        std::printf("  pad intro, kicks from 2.123s -> %.3f bpm @ %.4fs\n", r.bpm, r.firstBeatSec);
        CHECK(r.ok && std::abs(r.bpm - 128.0) < 0.02, "bpm %.3f", r.bpm);
        CHECK(std::abs(r.firstBeatSec - 2.123) < 0.006, "first beat %.4f vs 2.123", r.firstBeatSec);
    }
}

// Chords (root, third, fifth as MIDI offsets from the key's tonic), one per
// two bars at 124 BPM, as bright saw-ish pads with a bass root, over drums.
static std::vector<float> chordTrack(int tonic, const std::vector<std::array<int, 3>> &chords, double seconds, uint32_t sr)
{
    std::vector<float> x = clickTrack(124.0, 0.2, seconds, sr);
    const double chordSec = 8 * 60.0 / 124.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = double(i) / sr;
        const auto &c = chords[size_t(t / chordSec) % chords.size()];
        double v = 0.0;
        for (int n = 0; n < 4; ++n) {
            const int midi = n == 3 ? 36 + tonic + c[0] : 48 + tonic + c[size_t(n)];
            const double f = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            for (int h = 1; h <= 6; ++h)
                v += std::sin(2 * M_PI * f * h * t) / h;
        }
        x[i] += float(0.06 * v);
    }
    return x;
}

static void testKey()
{
    std::printf("key estimate\n");
    const std::vector<std::array<int, 3>> majorCad = {{0, 4, 7}, {5, 9, 12}, {7, 11, 14}, {0, 4, 7}};   // I IV V I
    const std::vector<std::array<int, 3>> minorCad = {{0, 3, 7}, {5, 8, 12}, {7, 11, 14}, {0, 3, 7}};   // i iv V i
    struct Case { int tonic; bool minor; } cases[] = {{9, true}, {0, false}, {6, true}, {3, false}, {2, true}};
    for (auto c : cases) {
        const int want = c.tonic + (c.minor ? 12 : 0);
        auto x = chordTrack(c.tonic, c.minor ? minorCad : majorCad, 60.0, 22050);
        KeyResult r = estimateKey(x, 22050);
        std::printf("  %-4s -> %-4s (%s, conf %.2f)\n", keys::standardName(want).c_str(), keys::standardName(r.key).c_str(),
                    keys::camelot(r.key).c_str(), r.confidence);
        CHECK(r.ok && r.key == want, "key %s vs %s", keys::standardName(r.key).c_str(), keys::standardName(want).c_str());
    }
}

static TrackBufferPtr *makeTrack(const std::vector<float> &mono, uint32_t sr)
{
    auto tb = std::make_shared<TrackBuffer>();
    tb->sampleRate = sr;
    tb->left = mono;
    tb->right = mono;
    tb->computeOverview();
    return new TrackBufferPtr(tb);
}

// Kick onsets in the output (seconds).
static std::vector<double> onsets(const std::vector<float> &inter, uint32_t sr, float thr = 0.5f)
{
    std::vector<double> o;
    size_t last = 0;
    bool armed = true;
    for (size_t i = 0; i < inter.size() / 2; ++i) {
        const float a = std::abs(inter[2 * i]);
        if (armed && a > thr && (o.empty() || i - last > sr / 10)) {
            o.push_back(double(i) / sr);
            last = i;
            armed = false;
        }
        if (!armed && i - last > sr / 10)
            armed = true;
    }
    return o;
}

static std::vector<float> renderSeconds(AudioEngine &e, double sec)
{
    std::vector<float> out(size_t(sec * e.sampleRate()) * 2);
    const int block = 512;
    for (size_t f = 0; f < out.size() / 2; f += block) {
        const int n = int(std::min<size_t>(block, out.size() / 2 - f));
        e.renderOffline(out.data() + f * 2, n);
    }
    return out;
}

// Kicks start ~10ms before the envelope crosses the threshold; measure relative
// deviation against the master grid using the median offset.
static void phaseStats(const std::vector<double> &on, double masterBpm, double fromSec, double &medianOff, double &maxDev)
{
    const double spb = 60.0 / masterBpm;
    std::vector<double> offs;
    for (double t : on)
        if (t > fromSec)
            offs.push_back(t - spb * std::round(t / spb));
    if (offs.empty()) {
        medianOff = maxDev = 1e9;
        return;
    }
    std::vector<double> s = offs;
    std::sort(s.begin(), s.end());
    medianOff = s[s.size() / 2];
    maxDev = 0;
    for (double d : offs)
        maxDev = std::max(maxDev, std::abs(d - medianOff));
}

static void testSync()
{
    std::printf("sync / phase lock\n");
    const uint32_t sr = 48000;
    AudioEngine e;
    e.initOffline(sr);
    e.setMasterBpm(125.0);
    e.setMasterVolume(1.0f);

    // A reference click on the master grid tells us the constant detector offset.
    auto ref = clickTrack(125.0, 0.0, 30.0, sr);
    double refOff, refDev;
    phaseStats(onsets([&] { std::vector<float> v(ref.size() * 2); for (size_t i = 0; i < ref.size(); ++i) v[2 * i] = v[2 * i + 1] = ref[i]; return v; }(), sr),
               125.0, 0.0, refOff, refDev);

    auto x = clickTrack(130.0, 0.37, 40.0, sr);
    e.post(0, {DeckCommand::Load, 0.0, 0.0, makeTrack(x, sr)});
    e.post(0, {DeckCommand::SetGrid, 130.0, 0.37});
    e.post(0, {DeckCommand::SetLoopEnabled, 0.0});
    e.post(0, {DeckCommand::SetSync, 1.0});
    renderSeconds(e, 1.3); // play pressed at an arbitrary master phase
    e.post(0, {DeckCommand::Play});
    auto out = renderSeconds(e, 20.0);
    const double startSec = 1.3;
    auto on = onsets(out, sr);
    for (double &t : on)
        t += startSec;
    double med, dev;
    phaseStats(on, 125.0, startSec + 2.0, med, dev);
    const double err = med - refOff;
    std::printf("  130 -> 125 bpm: %zu kicks, phase offset %.2f ms, jitter %.2f ms, speed %.4f\n",
                on.size(), err * 1000, dev * 1000, e.deck(0).pubSpeed.load());
    CHECK(std::abs(err) < 0.004, "phase offset %.2f ms", err * 1000);
    CHECK(dev < 0.004, "jitter %.2f ms", dev * 1000);
    // 20 s at 125 bpm = ~41 beats
    CHECK(on.size() > 38 && on.size() < 44, "kick count %zu", on.size());

    // Master tempo change while playing: should follow and stay in phase.
    e.setMasterBpm(128.0);
    auto out2 = renderSeconds(e, 12.0);
    auto on2 = onsets(out2, sr);
    // master beat phase continues from current master beat; measure against master grid
    const double beatNow = e.masterBeat();
    (void)beatNow;
    std::vector<double> ints;
    for (size_t i = 1; i < on2.size(); ++i)
        if (on2[i] > 4.0)
            ints.push_back(on2[i] - on2[i - 1]);
    double mean = 0;
    for (double d : ints)
        mean += d;
    mean /= std::max<size_t>(1, ints.size());
    std::printf("  master -> 128: mean interval %.4f s (expect %.4f)\n", mean, 60.0 / 128.0);
    CHECK(std::abs(mean - 60.0 / 128.0) < 0.002, "interval %.4f", mean);
}

// A track that's silent but for one click at track beat 40, scheduled to
// start at track beat 32 when the master reaches beat 24: the click must sound
// at master beat 32, though the track is at 130 bpm and the master at 125.
static void testPlayAt()
{
    std::printf("scheduled start\n");
    const uint32_t sr = 48000;
    AudioEngine e;
    e.initOffline(sr);
    e.setMasterBpm(125.0);
    e.setMasterVolume(1.0f);
    const double tBpm = 130.0, first = 0.21;
    std::vector<float> x(size_t(40.0 * sr), 0.0f);
    const size_t at = size_t((first + 40 * 60.0 / tBpm) * sr);
    for (size_t i = 0; i < size_t(0.12 * sr); ++i) {
        const double tt = double(i) / sr;
        x[at + i] = float(0.9 * std::sin(2 * M_PI * 55.0 * tt * (1.0 + 2.0 * std::exp(-tt * 40))) * std::exp(-tt * 18));
    }
    // the detector's own lag, from the same click at a known time
    double lag = 0.0;
    {
        std::vector<float> v(size_t(2 * sr) * 2, 0.0f);
        for (size_t i = 0; i < size_t(0.12 * sr); ++i)
            v[2 * (size_t(sr) + i)] = x[at + i];
        auto o = onsets(v, sr);
        lag = o.empty() ? 0.0 : o[0] - 1.0;
    }
    e.post(0, {DeckCommand::Load, 0.0, 0.0, makeTrack(x, sr)});
    e.post(0, {DeckCommand::SetGrid, tBpm, first});
    e.post(0, {DeckCommand::SetLoopEnabled, 0.0});
    e.post(0, {DeckCommand::SetSync, 1.0});
    e.post(0, {DeckCommand::PlayAt, 24.0, 32.0});
    auto out = renderSeconds(e, 20.0);
    auto on = onsets(out, sr);
    const double want = 32.0 * 60.0 / 125.0;
    const double got = on.empty() ? -1.0 : on[0] - lag;
    std::printf("  click at master beat 32: %.4f s (expect %.4f), %zu onsets\n", got, want, on.size());
    CHECK(on.size() == 1, "%zu onsets", on.size());
    CHECK(std::abs(got - want) < 0.005, "off by %.1f ms", (got - want) * 1000);
}

// Sections of a made-up track: 16 bars of hats, a 32-bar drop (kick and bass),
// a 16-bar breakdown (a pad), another 32-bar drop and an 8-bar outro.
static void testSections()
{
    std::printf("sections\n");
    const uint32_t sr = 22050;
    const double bpm = 140.0, bar = 4 * 60.0 / bpm;
    const int plan[] = {16, 32, 16, 32, 8};
    const bool drop[] = {false, true, false, true, false};
    std::vector<float> x(size_t(104 * bar * sr) + sr, 0.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    int b0 = 0;
    for (int s = 0; s < 5; ++s) {
        for (int b = b0; b < b0 + plan[s]; ++b) {
            for (int beat = 0; beat < 4; ++beat) {
                const size_t o = size_t((b * bar + beat * bar / 4) * sr);
                for (size_t i = 0; i < size_t(0.03 * sr); ++i) // a hat on the off-beat, always
                    x[o + size_t(bar / 8 * sr) + i] += 0.2f * u(rng) * float(std::exp(-double(i) / sr * 120));
                for (size_t i = 0; i < size_t(bar / 4 * sr); ++i) {
                    const double t = double(i) / sr;
                    if (drop[s]) // kick and a sub bass
                        x[o + i] += float(0.8 * std::sin(2 * M_PI * 50 * t * (1 + 2 * std::exp(-t * 40))) * std::exp(-t * 12)
                                          + 0.4 * std::sin(2 * M_PI * 49.0 * t));
                    else if (s == 2) // the breakdown's pad
                        x[o + i] += float(0.15 * std::sin(2 * M_PI * 440.0 * t) + 0.1 * std::sin(2 * M_PI * 659.3 * t));
                }
            }
        }
        b0 += plan[s];
    }
    StructureResult r = estimateStructure(x, sr, bpm, 0.0);
    std::printf("  %s\n", r.bars.c_str());
    CHECK(r.ok && r.sections.size() == 5, "%zu sections", r.sections.size());
    if (r.sections.size() == 5) {
        int at = 0;
        for (int s = 0; s < 5; ++s) {
            CHECK(std::abs(r.sections[size_t(s)].startSec - at * bar) < 0.01 && r.sections[size_t(s)].drop == drop[s],
                  "section %d at %.2f s (expect %.2f), drop %d", s, r.sections[size_t(s)].startSec, at * bar, int(r.sections[size_t(s)].drop));
            at += plan[s];
        }
    }
    CHECK(std::abs(r.firstDropSec - 16 * bar) < 0.01, "first drop %.2f s (expect %.2f)", r.firstDropSec, 16 * bar);
}

static void testKeyShiftAndLoop()
{
    std::printf("key shift + end loop\n");
    const uint32_t sr = 48000;
    AudioEngine e;
    e.initOffline(sr);
    e.setMasterBpm(120.0);
    e.setMasterVolume(1.0f);

    // 8 s of 440 Hz tone at "120 bpm", loop last 2 bars (beats 8..16 = 4..8 s)
    std::vector<float> x(size_t(8.0 * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5f * float(std::sin(2 * M_PI * 440.0 * double(i) / sr));
    e.post(0, {DeckCommand::Load, 0.0, 0.0, makeTrack(x, sr)});
    e.post(0, {DeckCommand::SetGrid, 120.0, 0.0});
    e.post(0, {DeckCommand::SetLoop, 8.0, 16.0});
    e.post(0, {DeckCommand::SetLoopEnabled, 1.0});
    e.post(0, {DeckCommand::SetSync, 1.0});
    e.deck(0).semitones.store(7);
    e.post(0, {DeckCommand::Play});
    auto out = renderSeconds(e, 20.0);

    // frequency in [1s,3s] by zero crossings
    int zc = 0;
    for (size_t i = size_t(1.0 * sr); i < size_t(3.0 * sr); ++i)
        if ((out[2 * i] >= 0) != (out[2 * i + 2] >= 0))
            ++zc;
    const double f = zc / 2.0 / 2.0;
    const double expect = 440.0 * std::pow(2.0, 7.0 / 12.0);
    std::printf("  +7 st: %.1f Hz (expect %.1f)\n", f, expect);
    CHECK(std::abs(f - expect) < 3.0, "freq %.1f", f);

    // still sounding long after the 8 s track would have ended
    double rms = 0;
    for (size_t i = size_t(15.0 * sr); i < size_t(19.0 * sr); ++i)
        rms += out[2 * i] * out[2 * i];
    rms = std::sqrt(rms / (4.0 * sr));
    const double pos = e.deck(0).pubPosFrame.load() / sr;
    std::printf("  after 20 s: rms %.3f, playing %d, position %.2fs (inside loop 4..8)\n", rms, int(e.deck(0).pubPlaying.load()), pos);
    CHECK(rms > 0.2, "loop went silent (rms %.3f)", rms);
    CHECK(e.deck(0).pubPlaying.load() && pos >= 4.0 && pos < 8.0, "position %.2f", pos);

    // without a loop the deck stops at the end
    e.post(0, {DeckCommand::SetLoopEnabled, 0.0});
    renderSeconds(e, 6.0);
    CHECK(!e.deck(0).pubPlaying.load(), "deck should stop at track end");
    e.collectGarbage();
}

// Jumps aimed just inside a loop's end, at different moments: the phase
// alignment may push them past the end, and they must still loop.
static void testJumpIntoLoopEnd()
{
    std::printf("jump into a loop near its end\n");
    const uint32_t sr = 48000;
    std::vector<float> x(size_t(30.0 * sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.5f * float(std::sin(2 * M_PI * 220.0 * double(i) / sr));
    int held = 0;
    const int trials = 8;
    for (int k = 0; k < trials; ++k) {
        AudioEngine e;
        e.initOffline(sr);
        e.setMasterBpm(120.0);
        e.post(0, {DeckCommand::Load, 0.0, 0.0, makeTrack(x, sr)});
        e.post(0, {DeckCommand::SetGrid, 120.0, 0.0});
        e.post(0, {DeckCommand::SetLoop, 8.0, 12.0}); // 4 s .. 6 s
        e.post(0, {DeckCommand::SetLoopEnabled, 1.0});
        e.post(0, {DeckCommand::SetSync, 1.0});
        e.post(0, {DeckCommand::Play});
        renderSeconds(e, 1.0 + 0.0625 * k); // a different master phase each time
        e.post(0, {DeckCommand::Seek, (6.0 - 0.01) * sr});
        renderSeconds(e, 4.0);
        const double pos = e.deck(0).pubPosFrame.load() / sr;
        held += pos >= 4.0 && pos < 6.0;
        e.collectGarbage();
    }
    std::printf("  %d / %d jumps to 10 ms before the end still loop\n", held, trials);
    CHECK(held == trials, "%d of %d jumps missed the loop", trials - held, trials);
}

int main()
{
    testTempo();
    testSync();
    testKeyShiftAndLoop();
    testKey();
    testPlayAt();
    testSections();
    testJumpIntoLoopEnd();
    std::printf(g_fail ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
