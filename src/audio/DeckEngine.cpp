#include "audio/DeckEngine.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>

using RubberBand::RubberBandStretcher;

namespace {
constexpr double kPllTauSec = 1.0;     // phase error time constant
constexpr double kPllMaxCorr = 0.03;   // max +-3% tempo correction
constexpr double kResyncBeats = 0.15;  // larger phase errors trigger a re-align
constexpr int kFadeFrames = 256;
} // namespace

DeckEngine::DeckEngine(uint32_t sampleRate)
    : m_sr(sampleRate)
{
    const auto opts = RubberBandStretcher::OptionProcessRealTime
        | RubberBandStretcher::OptionEngineFiner
        | RubberBandStretcher::OptionPitchHighConsistency
        | RubberBandStretcher::OptionChannelsTogether;
    m_rb = std::make_unique<RubberBandStretcher>(sampleRate, 2, opts, 1.0, 1.0);
    m_rb->setMaxProcessSize(4096);
    m_inL.resize(4096);
    m_inR.resize(4096);
    m_outL.resize(kMaxBlock);
    m_outR.resize(kMaxBlock);
}

DeckEngine::~DeckEngine()
{
    delete m_track;
}

double DeckEngine::targetBaseRatio(const MasterState &m) const
{
    // Rubber Band time ratio: >1 = longer = slower.
    if (m_sync && m_grid.valid() && m.bpm > 1.0)
        return m_grid.bpm / m.bpm;
    return 1.0;
}

// Shift a frame so the track's beat (or bar) phase matches the master, taking
// the nearest such position.
double DeckEngine::alignedFrame(double frame, const MasterState &m) const
{
    if (!m_sync || !m_grid.valid())
        return frame;
    const double period = m.phase == PhaseMode::Bar ? 4.0 : 1.0;
    const double tb = m_grid.beatAtFrame(frame, m_sr);
    const double d = wrapCentered(m.beat - tb, period);
    return m_grid.frameAtBeat(tb + d, m_sr);
}

void DeckEngine::recomputeLoopFrames()
{
    if (m_grid.valid() && m_loopEndBeat > m_loopStartBeat) {
        m_loopStartF = m_grid.frameAtBeat(m_loopStartBeat, m_sr);
        m_loopEndF = m_grid.frameAtBeat(m_loopEndBeat, m_sr);
    } else {
        m_loopStartF = m_loopEndF = 0.0;
    }
    m_feedInLoop = loopValid() && m_feedPos >= m_loopStartF && m_feedPos < m_loopEndF;
    m_audibleInLoop = loopValid() && m_audiblePos >= m_loopStartF && m_audiblePos < m_loopEndF;
}

void DeckEngine::startAt(double frame, double ratio, double pitch)
{
    m_rb->reset();
    m_rb->setTimeRatio(ratio);
    m_rb->setPitchScale(pitch);
    m_ratio = ratio;
    m_pitch = pitch;
    // Pre-roll with the real audio before the start point instead of silence,
    // then drop the stretcher's start delay so output frame 0 == `frame`.
    const double pad = double(m_rb->getPreferredStartPad());
    m_discard = m_rb->getStartDelay();
    m_audiblePos = frame;
    m_feedPos = frame - pad;
    m_feedInLoop = loopValid() && m_feedPos >= m_loopStartF && m_feedPos < m_loopEndF;
    m_audibleInLoop = loopValid() && m_audiblePos >= m_loopStartF && m_audiblePos < m_loopEndF;
    m_fadeIn = true;
    m_fadePos = 0;
}

TrackBufferPtr *DeckEngine::handle(const DeckCommand &cmd, const MasterState &m)
{
    TrackBufferPtr *garbage = nullptr;
    switch (cmd.type) {
    case DeckCommand::Load:
        garbage = m_track;
        m_track = cmd.track;
        m_playing = false;
        m_scheduled = false;
        m_pending = Pending::None;
        m_audiblePos = m_feedPos = cmd.a;
        recomputeLoopFrames();
        break;
    case DeckCommand::Play:
        m_scheduled = false;
        if (m_track && *m_track && !m_playing) {
            const double ratio = targetBaseRatio(m);
            startAt(alignedFrame(m_audiblePos, m), ratio, std::pow(2.0, semitones.load() / 12.0));
            m_playing = true;
            m_pending = Pending::None;
        }
        break;
    case DeckCommand::Pause:
        m_scheduled = false;
        if (m_playing)
            m_pending = Pending::Pause;
        break;
    case DeckCommand::Seek:
        m_scheduled = false;
        if (m_playing) {
            m_pending = Pending::Seek;
            m_pendingFrame = cmd.a;
        } else {
            m_audiblePos = m_feedPos = cmd.a;
            recomputeLoopFrames();
        }
        break;
    case DeckCommand::SetGrid:
        m_grid.bpm = cmd.a;
        m_grid.firstBeatSec = cmd.b;
        recomputeLoopFrames();
        break;
    case DeckCommand::SetLoop:
        m_loopStartBeat = cmd.a;
        m_loopEndBeat = cmd.b;
        recomputeLoopFrames();
        break;
    case DeckCommand::SetLoopEnabled:
        m_loopEnabled = cmd.a != 0.0;
        recomputeLoopFrames();
        break;
    case DeckCommand::SetSync:
        m_sync = cmd.a != 0.0;
        if (m_sync && m_playing) {
            m_pending = Pending::Seek; // snap into phase
            m_pendingFrame = m_audiblePos;
        }
        break;
    case DeckCommand::PlayAt:
        if (m_track && *m_track && !m_playing && m_grid.valid()) {
            m_scheduled = true;
            m_schedMasterBeat = cmd.a;
            m_schedTrackBeat = cmd.b;
            m_audiblePos = m_feedPos = m_grid.frameAtBeat(cmd.b, m_sr);
            recomputeLoopFrames();
        }
        break;
    case DeckCommand::None:
        break;
    }
    publish();
    return garbage;
}

void DeckEngine::feed(float *l, float *r, int count)
{
    const TrackBuffer &tb = **m_track;
    const int64_t frames = tb.frames();
    const bool loop = loopValid();
    for (int i = 0; i < count; ++i) {
        if (loop) {
            if (!m_feedInLoop && m_feedPos >= m_loopStartF && m_feedPos < m_loopEndF)
                m_feedInLoop = true;
            if (m_feedInLoop && m_feedPos >= m_loopEndF)
                m_feedPos -= (m_loopEndF - m_loopStartF);
        }
        const int64_t idx = int64_t(std::floor(m_feedPos));
        if (idx >= 0 && idx < frames) {
            l[i] = tb.left[size_t(idx)];
            r[i] = tb.right[size_t(idx)];
        } else {
            l[i] = r[i] = 0.0f;
        }
        m_feedPos += 1.0;
    }
}

void DeckEngine::render(float *outL, float *outR, int n, const MasterState &m)
{
    // a scheduled start: once the master is there, from where the track
    // would be by now (beat for beat: a synced deck keeps the master's tempo)
    if (m_scheduled && m_track && *m_track && m.beat >= m_schedMasterBeat) {
        m_scheduled = false;
        const double frame = m_grid.frameAtBeat(m_schedTrackBeat + (m.beat - m_schedMasterBeat), m_sr);
        startAt(frame, targetBaseRatio(m), std::pow(2.0, semitones.load() / 12.0));
        m_playing = true;
        m_pending = Pending::None;
    }
    if (!m_track || !*m_track || !m_playing) {
        m_level *= 0.9f;
        m_lastGain = 0.0f;
        publish();
        return;
    }

    // ---- tempo: base ratio + phase-locked loop correction ----
    const double base = targetBaseRatio(m);
    double ratio = base;
    m_phaseErr = 0.0;
    if (m_sync && m_grid.valid() && m_pending == Pending::None) {
        const double tb = m_grid.beatAtFrame(m_audiblePos, m_sr);
        m_phaseErr = wrapCentered(tb - m.beat, 1.0);
        if (std::abs(m_phaseErr) > kResyncBeats) {
            m_pending = Pending::Seek;
            m_pendingFrame = m_audiblePos;
        } else {
            const double bps = m.bpm / 60.0;
            const double corr = std::clamp(m_phaseErr / (kPllTauSec * bps), -kPllMaxCorr, kPllMaxCorr);
            ratio = base * (1.0 + corr);
        }
    }
    const double pitch = std::pow(2.0, semitones.load(std::memory_order_relaxed) / 12.0);
    if (ratio != m_ratio) {
        m_rb->setTimeRatio(ratio);
        m_ratio = ratio;
    }
    if (pitch != m_pitch) {
        m_rb->setPitchScale(pitch);
        m_pitch = pitch;
    }

    // ---- pull n frames out of the stretcher ----
    float *in[2] = {m_inL.data(), m_inR.data()};
    while (size_t(std::max(0, m_rb->available())) < size_t(n) + m_discard) {
        size_t req = m_rb->getSamplesRequired();
        if (req == 0)
            req = 256;
        req = std::min<size_t>(req, m_inL.size());
        feed(m_inL.data(), m_inR.data(), int(req));
        m_rb->process(in, req, false);
    }
    float *out[2] = {m_outL.data(), m_outR.data()};
    while (m_discard > 0) {
        const size_t k = std::min<size_t>(m_discard, size_t(kMaxBlock));
        m_rb->retrieve(out, k);
        m_discard -= k;
    }
    const size_t got = m_rb->retrieve(out, size_t(n));
    for (size_t i = got; i < size_t(n); ++i)
        m_outL[i] = m_outR[i] = 0.0f;

    // ---- gain: volume ramp, fade in after (re)start, fade out before seek/pause ----
    const float target = std::clamp(volume.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float g0 = m_lastGain, g1 = target;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        float g = g0 + (g1 - g0) * float(i + 1) / float(n);
        if (m_fadeIn)
            g *= std::min(1.0f, float(m_fadePos + i) / float(kFadeFrames));
        if (m_pending != Pending::None)
            g *= 1.0f - float(i + 1) / float(n);
        const float l = m_outL[i] * g, r = m_outR[i] * g;
        outL[i] += l;
        outR[i] += r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    m_lastGain = target;
    if (m_fadeIn) {
        m_fadePos += n;
        m_fadeIn = m_fadePos < kFadeFrames;
    }
    m_level = std::max(peak, m_level * 0.9f);

    // ---- advance the audible position (input frames heard) ----
    m_audiblePos += double(n) / ratio;
    if (loopValid()) {
        if (!m_audibleInLoop && m_audiblePos >= m_loopStartF && m_audiblePos < m_loopEndF)
            m_audibleInLoop = true;
        if (m_audibleInLoop && m_audiblePos >= m_loopEndF)
            m_audiblePos -= (m_loopEndF - m_loopStartF);
    }
    const double frames = double((*m_track)->frames());
    if (!m_audibleInLoop && m_audiblePos >= frames) {
        m_playing = false;
        m_pending = Pending::None;
        m_audiblePos = frames;
    }

    // ---- apply deferred actions (after this block's fade-out) ----
    if (m_pending == Pending::Pause) {
        m_playing = false;
        m_feedPos = m_audiblePos;
        m_pending = Pending::None;
    } else if (m_pending == Pending::Seek) {
        m_pending = Pending::None;
        double frame = alignedFrame(m_pendingFrame, MasterState{m.bpm, m.beat + n * m.bpm / 60.0 / m.sampleRate, m.sampleRate, m.phase});
        // A jump aimed inside the loop that the phase alignment pushed past its
        // end would miss the loop: wrap it back in.
        if (loopValid() && m_pendingFrame >= m_loopStartF && m_pendingFrame < m_loopEndF && frame >= m_loopEndF)
            frame -= m_loopEndF - m_loopStartF;
        startAt(frame, targetBaseRatio(m), pitch);
    }
    publish();
}

void DeckEngine::publish()
{
    pubPosFrame.store(m_audiblePos, std::memory_order_relaxed);
    pubPlaying.store(m_playing, std::memory_order_relaxed);
    pubLevel.store(m_level, std::memory_order_relaxed);
    pubPhaseError.store(m_phaseErr, std::memory_order_relaxed);
    pubSpeed.store(m_playing ? 1.0 / m_ratio : 1.0, std::memory_order_relaxed);
}
