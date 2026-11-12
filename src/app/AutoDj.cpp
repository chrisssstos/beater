#include "app/AutoDj.h"

#include "app/DeckController.h"
#include "app/Library.h"
#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinLeadBeats = 16.0;  // a mix needs 4 bars of room, else the next breakdown
constexpr double kMaxLeadBeats = 128.0; // and its intro plays for 32 bars at most
constexpr double kCutBeats = 0.25;      // the old track goes in the last sixteenth before the drop
constexpr double kFadeShare = 0.6;      // the new track is fully up after this much of the overlap
constexpr double kStopAfterBeats = 8.0; // the old deck stops two bars after the drop

double floorBar(double b) { return std::floor(b / 4.0 + 1e-6) * 4.0; }
double ceilBar(double b) { return std::ceil(b / 4.0 - 1e-6) * 4.0; }
} // namespace

AutoDj::AutoDj(AudioEngine *engine, Library *library, DeckController *a, DeckController *b,
               std::function<void(double)> setCrossfader, QObject *parent)
    : QObject(parent), m_engine(engine), m_library(library), m_a(a), m_b(b), m_setCrossfader(std::move(setCrossfader))
{
    setStatus(QStringLiteral("drop tracks to queue them"));
}

void AutoDj::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on) {
        cancel(QString());
        m_waiting = -1;
        m_queue.clear();
        emit queueChanged();
    }
    m_current = m_starting = -1;
    setStatus(on ? (onAir() >= 0 ? QStringLiteral("drop the next track") : QStringLiteral("drop tracks to queue them"))
                 : QString());
    emit enabledChanged();
}

QStringList AutoDj::queueTitles() const
{
    QStringList out;
    for (const QString &id : m_queue)
        out << m_library->record(id).title;
    return out;
}

void AutoDj::take(const QString &trackId)
{
    if (!m_enabled || trackId.isEmpty())
        return;
    m_queue << trackId;
    emit queueChanged();
    next();
}

void AutoDj::clearQueue()
{
    m_queue.clear();
    emit queueChanged();
}

int AutoDj::onAir() const
{
    if (m_starting >= 0)
        return m_starting;
    if (m_current >= 0 && deck(m_current)->playing())
        return m_current;
    if (m_a->playing())
        return 0;
    if (m_b->playing())
        return 1;
    return -1;
}

void AutoDj::next()
{
    if (m_waiting >= 0 || m_plan.active || m_queue.isEmpty())
        return;
    if (m_starting >= 0 && !deck(m_starting)->playing())
        return; // till the deck it just started says it's playing
    m_starting = -1;
    const int playing = onAir();
    const int target = playing < 0 ? 0 : 1 - playing;
    m_startFirst = playing < 0;
    m_waiting = target;
    m_waitingId = m_queue.takeFirst();
    emit queueChanged();
    deck(target)->setMix(QString(), 0, 0);
    deck(target)->loadRecord(m_library->record(m_waitingId));
    setStatus(QStringLiteral("loading %1 into %2").arg(m_library->record(m_waitingId).title, deck(target)->name()));
}

void AutoDj::tick(double masterBeat)
{
    m_masterBeat = masterBeat;
    if (!m_enabled)
        return;
    next();

    // the next track, loaded and analysed: start it, or plan its mix
    if (m_waiting >= 0) {
        DeckController *w = deck(m_waiting);
        const bool ready = w->loaded() && !w->loading() && w->trackId() == m_waitingId && w->bpm() > 0.0 && !w->analyzing();
        if (ready) {
            const int playing = onAir();
            if (m_startFirst || playing < 0 || playing == m_waiting) {
                setX(m_waiting == 0 ? -1.0 : 1.0);
                w->setIntroArmed(false); // auto: it runs on into its drop
                if (!w->playing())
                    w->play();
                setStatus(QStringLiteral("playing %1").arg(w->title()));
                m_current = m_starting = m_waiting;
                m_waiting = -1;
            } else if (plan(playing, m_waiting)) {
                m_waiting = -1;
            }
        }
    }

    if (!m_plan.active)
        return;
    DeckController *out = deck(m_plan.out), *in = deck(m_plan.in);
    if (!out->playing() && !m_plan.started) {
        cancel(QStringLiteral("%1 stopped: mix called off").arg(out->name()));
        return;
    }
    if (masterBeat >= m_plan.startM + 1.0)
        m_plan.started = true;
    if (m_plan.started && !in->playing() && masterBeat < m_plan.swapM) {
        cancel(QStringLiteral("%1 stopped: mix called off").arg(in->name()));
        return;
    }

    // the crossfader: the new track comes up over the breakdown, the old one is cut on the drop
    const double outSide = m_plan.out == 0 ? -1.0 : 1.0;
    double x = outSide;
    if (masterBeat >= m_plan.swapM) {
        x = -outSide;
    } else if (masterBeat >= m_plan.swapM - kCutBeats) {
        x = -outSide * (masterBeat - (m_plan.swapM - kCutBeats)) / kCutBeats;
    } else if (masterBeat >= m_plan.fadeEndM) {
        x = 0.0;
    } else if (masterBeat >= m_plan.startM) {
        const double t = (masterBeat - m_plan.startM) / std::max(1e-6, m_plan.fadeEndM - m_plan.startM);
        x = outSide * (1.0 - t * t * (3.0 - 2.0 * t));
    }
    setX(x);

    const auto bars = [](double beats) { return int(std::ceil(beats / 4.0 - 1e-6)); };
    if (masterBeat < m_plan.startM)
        setStatus(QStringLiteral("%1 comes in over %2's breakdown in %3 bars, its drop in %4")
                      .arg(in->name(), out->name()).arg(bars(m_plan.startM - masterBeat)).arg(bars(m_plan.swapM - masterBeat)));
    else if (masterBeat < m_plan.swapM)
        setStatus(QStringLiteral("mixing into %1: its drop in %2 bars").arg(in->name()).arg(bars(m_plan.swapM - masterBeat)));
    else
        setStatus(QStringLiteral("playing %1").arg(in->title()));

    if (masterBeat >= m_plan.swapM + kStopAfterBeats)
        finish();
}

bool AutoDj::plan(int outIdx, int inIdx)
{
    DeckController *out = deck(outIdx), *in = deck(inIdx);
    const BeatGrid og = out->grid(), ig = in->grid();
    if (!og.valid() || !ig.valid())
        return false;
    if (!out->sync())
        out->setSync(true);

    // the outgoing deck's beats on the master clock (a synced deck keeps them
    // in step: whole beats apart, whole bars with bar phase lock)
    const double now = m_engine->masterBeat();
    const double oNow = out->beat();
    const double period = m_engine->phaseMode() == PhaseMode::Bar ? 4.0 : 1.0;
    const double offset = std::round((now - oNow) / period) * period;

    // where it gives way: the next breakdown (after a drop) with room, else its outro, else its last bars
    const double earliest = ceilBar(oNow + 4.0);
    double swapO = -1.0, from = 0.0;
    bool seenDrop = false;
    for (const TrackSection &s : out->sectionsSec()) {
        if (s.drop) {
            seenDrop = true;
            continue;
        }
        if (!seenDrop)
            continue;
        const double a = std::max(out->barAtSec(s.startSec), earliest);
        const double e = out->barAtSec(s.endSec);
        if (e - a >= kMinLeadBeats) {
            swapO = e;
            from = a;
            break;
        }
    }
    QString where = QStringLiteral("breakdown");
    if (swapO < 0.0) {
        const double last = floorBar(og.beatAtSec(out->duration()));
        swapO = std::max(last, earliest);
        from = std::max(earliest, swapO - 64.0);
        where = QStringLiteral("ending");
    }

    // the incoming deck: its intro over that stretch, its first drop on its end
    const double firstBar = ceilBar(ig.beatAtSec(0.0));
    double drop = in->firstDropBeat();
    if (drop < firstBar)
        drop = std::min(firstBar + 64.0, floorBar(ig.beatAtSec(in->duration())));
    const double lead = std::min({floorBar(drop - firstBar), floorBar(swapO - from), kMaxLeadBeats});

    Plan p;
    p.active = true;
    p.out = outIdx;
    p.in = inIdx;
    p.inDrop = drop;
    p.inStart = drop - std::max(0.0, lead);
    p.startM = swapO - std::max(0.0, lead) + offset;
    p.swapM = swapO + offset;
    p.fadeEndM = p.startM + std::max(0.0, lead) * kFadeShare;
    p.inId = in->trackId();
    m_plan = p;

    setX(outIdx == 0 ? -1.0 : 1.0);
    in->scheduleStart(p.startM, p.inStart);
    out->setMix(QStringLiteral("out"), swapO - std::max(0.0, lead), swapO);
    in->setMix(QStringLiteral("in"), p.inStart, p.inDrop);
    setStatus(QStringLiteral("%1 comes in over %2's %3").arg(in->name(), out->name(), where));
    emit statusChanged();
    return true;
}

void AutoDj::finish()
{
    DeckController *out = deck(m_plan.out), *in = deck(m_plan.in);
    out->pause();
    out->setMix(QString(), 0, 0);
    in->setMix(QString(), 0, 0);
    setX(m_plan.in == 0 ? -1.0 : 1.0);
    m_current = m_plan.in;
    m_plan = Plan{};
    setStatus(QStringLiteral("playing %1").arg(in->title()) + (m_queue.isEmpty() ? QStringLiteral(" · drop the next track") : QString()));
    emit statusChanged();
}

void AutoDj::cancel(const QString &why)
{
    if (m_plan.active) {
        DeckController *out = deck(m_plan.out), *in = deck(m_plan.in);
        if (!m_plan.started)
            in->pause(); // drops the scheduled start
        out->setMix(QString(), 0, 0);
        in->setMix(QString(), 0, 0);
        m_plan = Plan{};
        emit statusChanged();
    }
    if (!why.isEmpty())
        setStatus(why);
}

void AutoDj::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void AutoDj::setX(double x)
{
    m_setCrossfader(x);
}
