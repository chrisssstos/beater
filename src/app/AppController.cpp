#include "app/AppController.h"

#include <QSettings>

#include <cmath>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_engine(std::make_unique<AudioEngine>())
{
    QSettings s;
    m_masterBpm = s.value("masterBpm", 125.0).toDouble();
    m_masterVolume = s.value("masterVolume", 0.85).toDouble();
    m_phaseMode = s.value("phaseMode", 0).toInt();
    m_crossfader = std::clamp(s.value("crossfader", 0.0).toDouble(), -1.0, 1.0);

    m_output = s.value("outputDevice").toString();
    openOutput();
    if (!m_engine->ready())
        m_engine->initOffline(48000); // no device at all: the UI still works, silently
    refreshOutputs();
    m_engine->setMasterBpm(m_masterBpm);
    m_engine->setMasterVolume(float(m_masterVolume * m_masterVolume));
    m_engine->setPhaseMode(PhaseMode(m_phaseMode));

    m_library = std::make_unique<Library>();
    for (int i = 0; i < 2; ++i)
        m_decks[i] = std::make_unique<DeckController>(i, m_engine.get(), m_library.get());
    applyCrossfader(); // the remembered position
    m_autoDj = std::make_unique<AutoDj>(m_engine.get(), m_library.get(), deckA(), deckB(),
                                        [this](double x) { setCrossfader(x); });
    connect(m_library.get(), &Library::recordChanged, this, [this](const QString &id) {
        for (auto &d : m_decks)
            d->refreshFromLibrary(id);
    });

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AppController::tick);
    m_timer.start();
}

AppController::~AppController()
{
    m_timer.stop();
    QSettings s;
    s.setValue("masterBpm", m_masterBpm);
    s.setValue("masterVolume", m_masterVolume);
    s.setValue("phaseMode", m_phaseMode);
    s.setValue("crossfader", m_crossfader);
    m_engine->stop();
}


void AppController::setStatus(bool ok, const QString &text)
{
    m_audioOk = ok;
    m_audioStatus = text;
    emit audioStatusChanged();
}

void AppController::openOutput()
{
    const auto rate = [this] {
        return QStringLiteral("%1 · %2 kHz")
            .arg(QString::fromStdString(m_engine->deviceName()))
            .arg(m_engine->sampleRate() / 1000.0, 0, 'f', 1);
    };
    std::string err;
    if (m_engine->switchDevice(m_output.toStdString(), &err)) {
        setStatus(true, rate());
        return;
    }
    // the chosen one isn't there: play through the default meanwhile
    if (!m_output.isEmpty()) {
        const QString why = QString::fromStdString(err);
        if (m_engine->switchDevice({}, &err)) {
            setStatus(false, why + QStringLiteral(" · using ") + rate());
            return;
        }
    }
    setStatus(false, QString::fromStdString(err));
}

void AppController::setOutputDevice(const QString &name)
{
    const QString n = name == QLatin1String("System default") ? QString() : name;
    if (n == m_output && m_audioOk)
        return;
    m_output = n;
    QSettings().setValue("outputDevice", m_output);
    openOutput();
}

void AppController::refreshOutputs()
{
    QStringList l{QStringLiteral("System default")};
    for (const std::string &n : m_engine->outputDevices())
        l << QString::fromStdString(n);
    if (l != m_outputs) {
        m_outputs = l;
        emit outputDevicesChanged();
        // the chosen device is back (plugged in again): use it
        if (!m_output.isEmpty() && !m_audioOk && m_outputs.contains(m_output))
            openOutput();
    }
}

void AppController::tick()
{
    m_engine->collectGarbage();
    // The device went away (unplugged): carry on through the default, and
    // come back to the chosen one when it returns (see refreshOutputs).
    if (m_engine->deviceLost()) {
        const QString lost = QString::fromStdString(m_engine->deviceName());
        std::string err;
        if (m_engine->switchDevice({}, &err))
            setStatus(false, lost + QStringLiteral(" went away · using ")
                                 + QString::fromStdString(m_engine->deviceName()));
        else
            setStatus(false, QString::fromStdString(err));
    }
    // Watchdog: a device that says it runs but whose clock has stood still
    // for 1.5 s (Core Audio rerouting under us...) gets opened again.
    const double beat = m_engine->masterBeat();
    if (beat != m_lastBeat || !m_engine->running()) {
        m_lastBeat = beat;
        m_beatStill.start();
    } else if (m_beatStill.elapsed() > 1500) {
        const QString name = QString::fromStdString(m_engine->deviceName());
        openOutput();
        if (m_audioOk)
            setStatus(false, name + QStringLiteral(" stalled · reopened · ") + QString::fromStdString(m_engine->deviceName()));
        m_beatStill.start();
    }
    for (auto &d : m_decks)
        d->poll();
    m_masterBeat = m_engine->masterBeat();
    m_masterLevel = m_engine->masterLevel();
    m_autoDj->tick(m_masterBeat);
    emit masterBeatChanged();
}

void AppController::setMasterBpm(double bpm)
{
    bpm = std::clamp(std::round(bpm * 100.0) / 100.0, 60.0, 200.0);
    if (bpm == m_masterBpm)
        return;
    m_masterBpm = bpm;
    m_engine->setMasterBpm(bpm);
    for (auto &d : m_decks)
        d->masterTempoChanged();
    emit masterBpmChanged();
}

void AppController::setMasterVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    if (v == m_masterVolume)
        return;
    m_masterVolume = v;
    m_engine->setMasterVolume(float(v * v));
    emit masterVolumeChanged();
}

// The crossfader on top of the faders: in the middle both decks play at their
// fader; towards one side the other deck fades out (a quarter cosine), gone at
// the end.
void AppController::setCrossfader(double x)
{
    x = std::clamp(std::round(x * 100.0) / 100.0, -1.0, 1.0);
    if (x == m_crossfader)
        return;
    m_crossfader = x;
    applyCrossfader();
    emit crossfaderChanged();
}

void AppController::applyCrossfader()
{
    const double x = m_crossfader;
    m_decks[0]->setCrossfadeGain(x <= 0.0 ? 1.0 : std::cos(x * M_PI / 2.0));
    m_decks[1]->setCrossfadeGain(x >= 0.0 ? 1.0 : std::cos(-x * M_PI / 2.0));
}

void AppController::setPhaseMode(int m)
{
    m = m ? 1 : 0;
    if (m == m_phaseMode)
        return;
    m_phaseMode = m;
    m_engine->setPhaseMode(PhaseMode(m));
    emit phaseModeChanged();
}

void AppController::loadToDeck(int row, int deck)
{
    const QString id = m_library->idAt(row);
    if (id.isEmpty() || deck < 0 || deck > 1)
        return;
    if (m_autoDj->enabled())
        return m_autoDj->take(id);
    m_decks[deck]->loadRecord(m_library->record(id));
}

void AppController::loadToFreeDeck(int row)
{
    // Prefer an empty deck, then one that isn't playing, then the quieter one.
    int target = 0;
    if (!m_decks[0]->loaded())
        target = 0;
    else if (!m_decks[1]->loaded())
        target = 1;
    else if (!m_decks[0]->playing())
        target = 0;
    else if (!m_decks[1]->playing())
        target = 1;
    else
        target = m_decks[0]->volume() <= m_decks[1]->volume() ? 0 : 1;
    loadToDeck(row, target);
}

void AppController::loadUrlsToDeck(const QList<QUrl> &urls, int deck)
{
    if (deck < 0 || deck > 1)
        return;
    const QStringList ids = m_library->addUrls(urls);
    if (m_autoDj->enabled()) {
        for (const QString &id : ids)
            m_autoDj->take(id);
        return;
    }
    if (!ids.isEmpty())
        m_decks[deck]->loadRecord(m_library->record(ids.first()));
}

void AppController::matchMasterTo(int deck)
{
    if (deck >= 0 && deck < 2 && m_decks[deck]->bpm() > 0)
        setMasterBpm(m_decks[deck]->bpm());
}
