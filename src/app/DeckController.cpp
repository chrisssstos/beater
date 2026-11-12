#include "app/DeckController.h"

#include "app/Library.h"
#include "audio/AudioEngine.h"
#include "audio/Decoder.h"
#include "core/Key.h"

#include <QDateTime>
#include <QtConcurrent/QtConcurrent>

#include <cmath>

DeckController::DeckController(int index, AudioEngine *engine, Library *library, QObject *parent)
    : QObject(parent)
    , m_index(index)
    , m_engine(engine)
    , m_library(library)
{
    applyGain();
    m_tapClock.start();
    connect(m_library, &Library::camelotChanged, this, &DeckController::keyChanged);
}

double DeckController::beat() const
{
    return m_rec.grid().valid() ? m_rec.grid().beatAtSec(m_position) : 0.0;
}

QString DeckController::keyText() const
{
    return m_library->keyText(m_rec.key);
}

QString DeckController::shiftedKeyText() const
{
    return m_library->keyText(keys::transpose(m_rec.key, m_keyShift));
}

QStringList DeckController::keyNames() const
{
    QStringList l;
    for (int k = 0; k < 24; ++k)
        l << m_library->keyText(k);
    return l;
}

QString DeckController::activeLoop() const
{
    if (m_customOn && hasCustom())
        return QStringLiteral("custom");
    if (m_introArmed && m_introEnd > m_introStart)
        return QStringLiteral("intro");
    if (m_endArmed && m_endEnd > m_endStart)
        return QStringLiteral("end");
    return QString();
}

double DeckController::activeLoopStartBeat() const
{
    const QString a = activeLoop();
    return a == u"custom" ? m_customStart : a == u"intro" ? m_introStart : a == u"end" ? m_endStart : 0.0;
}

double DeckController::activeLoopEndBeat() const
{
    const QString a = activeLoop();
    return a == u"custom" ? m_customEnd : a == u"intro" ? m_introEnd : a == u"end" ? m_endEnd : 0.0;
}

bool DeckController::inLoop() const
{
    const double a = activeLoopStartBeat(), e = activeLoopEndBeat();
    if (e <= a || !m_rec.grid().valid())
        return false;
    const double b = beat();
    return b >= a && b < e;
}

// ---------------------------------------------------------------- loading

void DeckController::loadRecord(const TrackRecord &rec)
{
    const int serial = ++m_loadSerial;
    m_loading = true;
    m_error.clear();
    emit loadingChanged();
    emit errorChanged();
    const QString path = rec.path;
    const QString id = rec.id;
    const uint32_t sr = m_engine->sampleRate();
    QtConcurrent::run([path, sr]() -> std::shared_ptr<TrackBuffer> {
        std::string err;
        return decoder::decodeStereo(path.toStdString(), sr, &err);
    }).then(this, [this, serial, id](std::shared_ptr<TrackBuffer> tb) {
        if (serial != m_loadSerial)
            return; // a newer load superseded this one
        m_loading = false;
        emit loadingChanged();
        if (!tb) {
            m_error = QStringLiteral("Could not decode this file");
            emit errorChanged();
            return;
        }
        m_buffer = tb;
        m_rec = m_library->record(id);
        if (m_rec.durationSec <= 0.0) {
            m_rec.durationSec = tb->durationSec();
            m_library->update(m_rec);
        }
        m_keyShift = 0;
        m_engine->deck(m_index).semitones.store(0);
        m_introArmed = true; // every track starts looping its intro
        m_customOn = false;
        m_customPending = false;
        m_loopSwitchedAt.start();
        m_loadedAt.start();
        m_position = 0.0;
        m_preview = -1.0;
        m_previewHold.invalidate();
        ++m_waveRev;

        m_engine->post(m_index, {DeckCommand::Load, 0.0, 0.0, new TrackBufferPtr(m_buffer)});
        sendGrid();
        computeLoop();
        sendLoop();
        m_engine->post(m_index, {DeckCommand::SetSync, m_sync ? 1.0 : 0.0});

        emit trackChanged();
        emit gridChanged();
        emit analyzingChanged();
        emit keyChanged();
        emit loopChanged();
        emit positionChanged();
        updatePlayingBpm();
    });
}

void DeckController::refreshFromLibrary(const QString &id)
{
    if (id != m_rec.id || !m_buffer)
        return;
    const TrackRecord r = m_library->record(id);
    const bool gridDiff = r.bpm != m_rec.bpm || r.firstBeatSec != m_rec.firstBeatSec;
    const bool manualDiff = r.gridManual != m_rec.gridManual;
    const bool keyDiff = r.key != m_rec.key || r.keyManual != m_rec.keyManual;
    const bool statusDiff = r.status != m_rec.status;
    const bool loopDiff = r.loopStartBeat != m_rec.loopStartBeat || r.loopEndBeat != m_rec.loopEndBeat
                       || r.introStartBeat != m_rec.introStartBeat || r.introEndBeat != m_rec.introEndBeat
                       || r.customStartBeat != m_rec.customStartBeat || r.customEndBeat != m_rec.customEndBeat;
    const bool titleDiff = r.title != m_rec.title;
    const bool sectionsDiff = r.sections != m_rec.sections || r.firstDropSec != m_rec.firstDropSec;
    m_rec = r;
    if (gridDiff) {
        sendGrid();
        emit gridChanged();
        updatePlayingBpm();
    }
    if (gridDiff || loopDiff) {
        computeLoop();
        sendLoop();
        emit loopChanged();
    }
    if ((manualDiff || sectionsDiff) && !gridDiff)
        emit gridChanged();
    if (keyDiff)
        emit keyChanged();
    if (statusDiff)
        emit analyzingChanged();
    if (titleDiff)
        emit trackChanged();
}

void DeckController::commitRecord()
{
    if (!m_rec.id.isEmpty())
        m_library->update(m_rec);
}

// ---------------------------------------------------------------- engine sync

void DeckController::sendGrid()
{
    m_engine->post(m_index, {DeckCommand::SetGrid, m_rec.bpm, m_rec.firstBeatSec});
}

void DeckController::computeLoop()
{
    const BeatGrid g = m_rec.grid();
    const bool ready = g.valid() && m_buffer;

    if (m_rec.hasManualLoop()) {
        m_endStart = m_rec.loopStartBeat;
        m_endEnd = m_rec.loopEndBeat;
    } else if (ready) {
        // last whole bars of the track
        m_endEnd = g.lastLoopBeat(m_buffer->durationSec());
        m_endStart = std::max(g.firstLoopBeat(), m_endEnd - 4.0 * m_endBars);
    } else {
        m_endStart = m_endEnd = 0.0;
    }
    if (m_endEnd <= m_endStart)
        m_endStart = m_endEnd = 0.0;

    if (m_rec.hasManualIntro()) {
        m_introStart = m_rec.introStartBeat;
        m_introEnd = m_rec.introEndBeat;
    } else if (ready) {
        // first whole bars of the track
        m_introStart = g.firstLoopBeat();
        m_introEnd = std::min(m_introStart + 4.0 * m_introBars, g.lastLoopBeat(m_buffer->durationSec()));
    } else {
        m_introStart = m_introEnd = 0.0;
    }
    if (m_introEnd <= m_introStart)
        m_introStart = m_introEnd = 0.0;

    if (m_rec.hasCustomLoop()) {
        m_customStart = m_rec.customStartBeat;
        m_customEnd = m_rec.customEndBeat;
        m_customPending = false;
    } else if (!m_customPending) {
        m_customStart = m_customEnd = 0.0;
    }
}

void DeckController::sendLoop()
{
    // the engine loops one region: the one in use, if any
    const double a = activeLoopStartBeat(), b = activeLoopEndBeat();
    m_engine->post(m_index, {DeckCommand::SetLoop, a, b});
    m_engine->post(m_index, {DeckCommand::SetLoopEnabled, b > a ? 1.0 : 0.0});
}

void DeckController::jumpInto(double startBeat, double endBeat)
{
    // in phase, early in the loop (not at its edge, where it could be missed)
    if (endBeat > startBeat)
        jumpToBeat(startBeat, endBeat - startBeat);
}

void DeckController::updatePlayingBpm()
{
    double b = m_rec.bpm;
    if (m_sync && m_rec.bpm > 0.0)
        b = m_engine->masterBpm();
    if (b != m_playingBpm) {
        m_playingBpm = b;
        emit playingBpmChanged();
    }
}

void DeckController::poll()
{
    const DeckEngine &d = m_engine->deck(m_index);
    const uint32_t sr = m_engine->sampleRate();
    const double pos = d.pubPosFrame.load(std::memory_order_relaxed) / double(sr);
    const bool playing = d.pubPlaying.load(std::memory_order_relaxed);
    const double level = d.pubLevel.load(std::memory_order_relaxed);
    const double err = d.pubPhaseError.load(std::memory_order_relaxed);
    if (m_previewHold.isValid() && m_previewHold.elapsed() >= 150) {
        // the seek has landed: back to the engine's position
        m_preview = -1.0;
        m_previewHold.invalidate();
        emit positionChanged();
    }
    // The playhead left the intro or the In / Out loop behind (seeked, dragged):
    // release it, so the end loop catches the track. Only while playing, and
    // not just after a load or a switch, when the engine may still report the
    // position from before.
    if (playing && m_rec.grid().valid() && m_preview < 0.0 && m_loadedAt.isValid() && m_loadedAt.elapsed() > 200
        && m_loopSwitchedAt.isValid() && m_loopSwitchedAt.elapsed() > 200) {
        const QString a = activeLoop();
        if ((a == u"intro" || a == u"custom") && m_rec.grid().beatAtSec(pos) > activeLoopEndBeat() + 0.25) {
            if (a == u"intro")
                setIntroArmed(false);
            else
                setCustomOn(false);
        }
    }
    if (std::abs(pos - m_position) > 0.0005 || err != m_phaseError) {
        m_position = pos;
        m_phaseError = err;
        emit positionChanged();
    }
    if (playing != m_playing) {
        m_playing = playing;
        emit playingChanged();
    }
    if (std::abs(level - m_level) > 0.002) {
        m_level = level;
        emit levelChanged();
    }
}

// ---------------------------------------------------------------- transport

void DeckController::togglePlay()
{
    m_playing ? pause() : play();
}

void DeckController::play()
{
    if (!m_buffer)
        return;
    if (!m_playing) {
        m_engine->post(m_index, {DeckCommand::Play});
        return;
    }
    // again while playing: from the start (the first bar line), in phase
    const BeatGrid g = m_rec.grid();
    if (g.valid())
        jumpToBeat(g.firstLoopBeat());
    else
        m_engine->post(m_index, {DeckCommand::Seek, 0.0});
}

void DeckController::jumpToBeat(double target, double within)
{
    // Playing (and synced), the deck must keep its place in the beat (in the
    // bar, with bar phase lock): land at or just after the target, at the
    // same phase. Paused: on the target.
    const BeatGrid g = m_rec.grid();
    if (!g.valid() || !m_buffer)
        return;
    if (m_playing) {
        const double b = beat();
        const bool bar = m_engine->phaseMode() == PhaseMode::Bar;
        double off = bar ? std::fmod(std::fmod(b - target, 4.0) + 4.0, 4.0) : b - std::floor(b);
        if (within > 0.0 && off >= within)
            off = std::fmod(off, within);
        target += off;
    }
    const double sec = g.secAtBeat(target);
    m_engine->post(m_index, {DeckCommand::Seek, sec * m_engine->sampleRate()});
    if (!m_playing) {
        m_position = sec;
        emit positionChanged();
    }
}

void DeckController::pause()
{
    m_engine->post(m_index, {DeckCommand::Pause});
}

void DeckController::cue()
{
    if (!m_buffer)
        return;
    if (m_playing)
        pause();
    m_engine->post(m_index, {DeckCommand::Seek, 0.0});
    m_position = 0.0;
    emit positionChanged();
}

double DeckController::snapToBeat(double sec) const
{
    const BeatGrid g = m_rec.grid();
    if (!g.valid())
        return sec;
    return g.secAtBeat(std::round(g.beatAtSec(sec)));
}

void DeckController::seek(double sec)
{
    if (!m_buffer)
        return;
    sec = std::clamp(sec, 0.0, duration());
    const double t = snapToBeat(sec);
    m_engine->post(m_index, {DeckCommand::Seek, t * m_engine->sampleRate()});
    if (!m_playing) {
        m_position = t;
        emit positionChanged();
    }
}

void DeckController::setPreview(double sec)
{
    if (!m_buffer)
        return;
    m_preview = std::clamp(sec, 0.0, duration());
    m_previewHold.invalidate();
    emit positionChanged();
}

void DeckController::commitPreview()
{
    if (m_preview < 0.0)
        return;
    seek(m_preview);
    m_previewHold.start();
}

double DeckController::shownPosition() const
{
    if (m_preview >= 0.0 && (!m_previewHold.isValid() || m_previewHold.elapsed() < 150))
        return m_preview;
    return m_position;
}

void DeckController::jumpBeats(double beats)
{
    if (!m_buffer || !m_rec.grid().valid())
        return;
    const double t = std::clamp(m_position + beats * m_rec.grid().secPerBeat(), 0.0, duration());
    m_engine->post(m_index, {DeckCommand::Seek, t * m_engine->sampleRate()});
    if (!m_playing) {
        m_position = t;
        emit positionChanged();
    }
}

// ---------------------------------------------------------------- grid

void DeckController::setBpm(double bpm)
{
    if (m_rec.id.isEmpty() || !(bpm > 0))
        return;
    bpm = std::clamp(std::round(bpm * 100.0) / 100.0, 40.0, 250.0);
    if (bpm == m_rec.bpm)
        return;
    m_rec.bpm = bpm;
    m_rec.gridManual = true;
    sendGrid();
    computeLoop();
    sendLoop();
    commitRecord();
    emit gridChanged();
    emit loopChanged();
    updatePlayingBpm();
}

void DeckController::tap()
{
    const qint64 now = m_tapClock.elapsed();
    if (!m_taps.isEmpty() && now - m_taps.last() > 2000)
        m_taps.clear();
    m_taps.append(now);
    while (m_taps.size() > 16)
        m_taps.removeFirst();
    if (m_taps.size() >= 4) {
        const double ms = double(m_taps.last() - m_taps.first()) / double(m_taps.size() - 1);
        if (ms > 0)
            setBpm(std::round(60000.0 / ms * 10.0) / 10.0);
    }
}

void DeckController::setDownbeatHere()
{
    setDownbeatAt(m_position);
}

void DeckController::setDownbeatAt(double sec)
{
    if (m_rec.id.isEmpty() || !m_rec.grid().valid())
        return;
    const double bar = 4.0 * m_rec.grid().secPerBeat();
    m_rec.firstBeatSec = sec - bar * std::floor(sec / bar);
    m_rec.gridManual = true;
    sendGrid();
    computeLoop();
    sendLoop();
    commitRecord();
    emit gridChanged();
    emit loopChanged();
}

void DeckController::nudgeGrid(double ms)
{
    if (m_rec.id.isEmpty() || !m_rec.grid().valid())
        return;
    m_rec.firstBeatSec += ms / 1000.0;
    m_rec.gridManual = true;
    sendGrid();
    computeLoop();
    sendLoop();
    commitRecord();
    emit gridChanged();
    emit loopChanged();
}

void DeckController::autoGrid()
{
    if (!m_rec.id.isEmpty())
        m_library->autoDetect(m_rec.id, true, false);
}

// ---------------------------------------------------------------- key

void DeckController::autoKey()
{
    if (!m_rec.id.isEmpty())
        m_library->autoDetect(m_rec.id, false, true);
}

void DeckController::setKey(int key)
{
    if (m_rec.id.isEmpty())
        return;
    m_rec.key = keys::valid(key) ? key : -1;
    m_rec.keyManual = true;
    commitRecord();
    emit keyChanged();
}

void DeckController::setKeyShift(int st)
{
    st = std::clamp(st, -12, 12);
    if (st == m_keyShift)
        return;
    m_keyShift = st;
    m_engine->deck(m_index).semitones.store(st);
    emit keyChanged();
}

// ---------------------------------------------------------------- loops

void DeckController::setSync(bool on)
{
    if (on == m_sync)
        return;
    m_sync = on;
    m_engine->post(m_index, {DeckCommand::SetSync, on ? 1.0 : 0.0});
    emit syncChanged();
    updatePlayingBpm();
}

double DeckController::barAtSec(double sec) const
{
    const BeatGrid g = m_rec.grid();
    return g.valid() ? std::round(g.beatAtSec(sec) / 4.0) * 4.0 : 0.0;
}

QVariantList DeckController::sections() const
{
    QVariantList out;
    if (!m_rec.grid().valid())
        return out;
    for (const TrackSection &s : m_rec.sections)
        out.append(QVariantMap{{"start", barAtSec(s.startSec)}, {"end", barAtSec(s.endSec)}, {"drop", s.drop}});
    return out;
}

double DeckController::firstDropBeat() const
{
    return m_rec.firstDropSec >= 0.0 && m_rec.grid().valid() ? barAtSec(m_rec.firstDropSec) : -1.0;
}

void DeckController::setMix(const QString &role, double startBeat, double endBeat)
{
    if (role == m_mixRole && startBeat == m_mixStart && endBeat == m_mixEnd)
        return;
    m_mixRole = role;
    m_mixStart = startBeat;
    m_mixEnd = endBeat;
    emit mixChanged();
}

void DeckController::scheduleStart(double masterBeat, double trackBeat)
{
    if (!m_buffer || !m_rec.grid().valid())
        return;
    m_introArmed = false;
    m_customOn = false;
    m_customPending = false;
    m_loopSwitchedAt.start();
    sendLoop();
    emit loopChanged();
    if (!m_sync)
        setSync(true);
    m_engine->post(m_index, {DeckCommand::PlayAt, masterBeat, trackBeat});
}

void DeckController::setIntroArmed(bool on)
{
    if (on == m_introArmed)
        return;
    m_introArmed = on;
    m_loopSwitchedAt.start();
    sendLoop();
    if (on && activeLoop() == u"intro" && beat() >= m_introEnd)
        jumpInto(m_introStart, m_introEnd);
    emit loopChanged();
}

void DeckController::setCustomOn(bool on)
{
    if (on && !hasCustom())
        return;
    if (on == m_customOn)
        return;
    m_customOn = on;
    m_loopSwitchedAt.start();
    sendLoop();
    if (on && beat() >= m_customEnd)
        jumpInto(m_customStart, m_customEnd);
    emit loopChanged();
}

void DeckController::setEndArmed(bool on)
{
    if (on == m_endArmed)
        return;
    m_endArmed = on;
    sendLoop();
    emit loopChanged();
}

void DeckController::loopIn()
{
    if (!m_buffer || !m_rec.grid().valid())
        return;
    // In a closed In / Out loop that's running: back to its In (in phase, inside the loop)
    if (m_customOn && hasCustom()) {
        jumpToBeat(m_customStart, m_customEnd - m_customStart);
        return;
    }
    // a new In: any In / Out loop goes, the next Out closes this one
    m_customStart = m_customEnd = std::round(beat());
    m_customPending = true;
    m_customOn = false;
    if (m_rec.hasCustomLoop()) {
        m_rec.customStartBeat = m_rec.customEndBeat = 0.0;
        commitRecord();
    }
    sendLoop();
    emit loopChanged();
}

void DeckController::loopOut()
{
    if (!m_buffer || !m_rec.grid().valid() || (!m_customPending && !hasCustom()))
        return;
    const double b = beat();
    const double len = std::max(1.0, std::round(b - m_customStart));
    if (b < m_customStart)
        return;
    m_customEnd = m_customStart + len;
    m_customPending = false;
    m_rec.customStartBeat = m_customStart;
    m_rec.customEndBeat = m_customEnd;
    commitRecord();
    m_customOn = true;
    m_loopSwitchedAt.start();
    sendLoop();
    // the Out is (about) now: back to the In, in phase
    jumpInto(m_customStart, m_customEnd);
    emit loopChanged();
}

void DeckController::loopHere()
{
    if (!m_buffer || !m_rec.grid().valid())
        return;
    const double start = std::floor(beat() / 4.0) * 4.0;
    setCustomBeats(start, start + 4.0 * m_customBars);
    setCustomOn(true);
}

void DeckController::setLoopLength(int bars)
{
    bars = std::clamp(bars, 1, 64);
    m_customBars = bars; // the next Here
    const QString a = activeLoop();
    if (a == u"custom") {
        setCustomBeats(m_customStart, m_customStart + 4.0 * bars);
    } else if (a == u"intro") {
        m_introBars = bars;
        if (m_rec.hasManualIntro()) {
            m_rec.introEndBeat = m_rec.introStartBeat + 4.0 * bars;
            commitRecord();
        }
    } else if (a == u"end") {
        m_endBars = bars;
        if (m_rec.hasManualLoop()) {
            m_rec.loopEndBeat = m_rec.loopStartBeat + 4.0 * bars;
            commitRecord();
        }
    }
    computeLoop();
    sendLoop();
    emit loopChanged();
}

// The three regions take beats on the track grid; at least a beat long.
static bool orderedLoop(double &a, double &b)
{
    if (b < a)
        std::swap(a, b);
    return b - a >= 1.0;
}

void DeckController::setIntroBeats(double startBeat, double endBeat)
{
    if (m_rec.id.isEmpty() || !m_rec.grid().valid() || !orderedLoop(startBeat, endBeat))
        return;
    m_rec.introStartBeat = startBeat;
    m_rec.introEndBeat = endBeat;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::autoIntro()
{
    if (m_rec.id.isEmpty())
        return;
    m_rec.introStartBeat = m_rec.introEndBeat = 0.0;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::setCustomBeats(double startBeat, double endBeat)
{
    if (m_rec.id.isEmpty() || !m_rec.grid().valid() || !orderedLoop(startBeat, endBeat))
        return;
    m_rec.customStartBeat = startBeat;
    m_rec.customEndBeat = endBeat;
    m_customPending = false;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::clearCustom()
{
    m_rec.customStartBeat = m_rec.customEndBeat = 0.0;
    m_customPending = false;
    m_customOn = false;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::setEndBeats(double startBeat, double endBeat)
{
    if (m_rec.id.isEmpty() || !m_rec.grid().valid() || !orderedLoop(startBeat, endBeat))
        return;
    m_rec.loopStartBeat = startBeat;
    m_rec.loopEndBeat = endBeat;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::autoEnd()
{
    if (m_rec.id.isEmpty())
        return;
    m_rec.loopStartBeat = m_rec.loopEndBeat = 0.0;
    commitRecord();
    computeLoop();
    sendLoop();
    emit loopChanged();
}

void DeckController::setVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (v == m_volume)
        return;
    m_volume = v;
    applyGain();
    emit volumeChanged();
}

void DeckController::setCrossfadeGain(double g)
{
    g = std::clamp(g, 0.0, 1.0);
    if (g == m_xfadeGain)
        return;
    m_xfadeGain = g;
    applyGain();
}

void DeckController::applyGain()
{
    m_engine->deck(m_index).volume.store(float(m_volume * m_volume * m_xfadeGain)); // squared: a fader taper
}
