#pragma once
#include "app/AutoDj.h"
#include "app/DeckController.h"
#include "app/Library.h"
#include "audio/AudioEngine.h"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created in main.cpp")

    Q_PROPERTY(double masterBpm READ masterBpm WRITE setMasterBpm NOTIFY masterBpmChanged)
    Q_PROPERTY(double masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)
    Q_PROPERTY(int phaseMode READ phaseMode WRITE setPhaseMode NOTIFY phaseModeChanged)
    //! -1 (all A) .. 0 (both at their faders) .. 1 (all B). Remembered.
    Q_PROPERTY(double crossfader READ crossfader WRITE setCrossfader NOTIFY crossfaderChanged)
    Q_PROPERTY(double masterBeat READ masterBeat NOTIFY masterBeatChanged)
    Q_PROPERTY(double masterLevel READ masterLevel NOTIFY masterBeatChanged)
    Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioStatusChanged)
    Q_PROPERTY(bool audioOk READ audioOk NOTIFY audioStatusChanged)
    // Output devices: "System default" first, then each by name. outputDevice
    // is the chosen one ("" = system default); remembered.
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY outputDevicesChanged)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY audioStatusChanged)
    Q_PROPERTY(DeckController *deckA READ deckA CONSTANT)
    Q_PROPERTY(DeckController *deckB READ deckB CONSTANT)
    Q_PROPERTY(Library *library READ library CONSTANT)
    Q_PROPERTY(AutoDj *autoDj READ autoDj CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    double masterBpm() const { return m_masterBpm; }
    void setMasterBpm(double bpm);
    double masterVolume() const { return m_masterVolume; }
    void setMasterVolume(double v);
    int phaseMode() const { return m_phaseMode; }
    double crossfader() const { return m_crossfader; }
    void setCrossfader(double x);
    void setPhaseMode(int m);
    double masterBeat() const { return m_masterBeat; }
    double masterLevel() const { return m_masterLevel; }
    QString audioStatus() const { return m_audioStatus; }
    bool audioOk() const { return m_audioOk; }
    QStringList outputDevices() const { return m_outputs; }
    QString outputDevice() const { return m_output; }
    void setOutputDevice(const QString &name);
    Q_INVOKABLE void refreshOutputs();

    DeckController *deckA() const { return m_decks[0].get(); }
    DeckController *deckB() const { return m_decks[1].get(); }
    Library *library() const { return m_library.get(); }
    AutoDj *autoDj() const { return m_autoDj.get(); }

    Q_INVOKABLE void nudgeMasterBpm(double delta) { setMasterBpm(m_masterBpm + delta); }
    Q_INVOKABLE void loadToDeck(int row, int deck);
    Q_INVOKABLE void loadToFreeDeck(int row);
    Q_INVOKABLE void addUrls(const QList<QUrl> &urls) { m_library->addUrls(urls); }
    // Files dropped on a deck: into the library, and the first one onto the deck.
    // With Auto on, tracks go into its queue instead (all of them).
    Q_INVOKABLE void loadUrlsToDeck(const QList<QUrl> &urls, int deck);
    Q_INVOKABLE void matchMasterTo(int deck);

signals:
    void masterBpmChanged();
    void masterVolumeChanged();
    void phaseModeChanged();
    void crossfaderChanged();
    void masterBeatChanged();
    void audioStatusChanged();
    void outputDevicesChanged();

private:
    void tick();
    void openOutput(); // the chosen device, else the system default
    void setStatus(bool ok, const QString &text);
    void applyCrossfader();

    std::unique_ptr<AudioEngine> m_engine;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<DeckController> m_decks[2];
    std::unique_ptr<AutoDj> m_autoDj;
    QTimer m_timer;

    double m_masterBpm = 125.0;
    double m_masterVolume = 0.85;
    int m_phaseMode = 0;
    double m_crossfader = 0.0;
    double m_masterBeat = 0.0;
    double m_masterLevel = 0.0;
    QString m_audioStatus;
    bool m_audioOk = false;
    QStringList m_outputs;
    QString m_output;
    double m_lastBeat = -1.0;    // watchdog: the audio clock must keep moving
    QElapsedTimer m_beatStill;
};
