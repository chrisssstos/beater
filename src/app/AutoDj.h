#pragma once
#include <QObject>
#include <functional>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class AudioEngine;
class DeckController;
class Library;

// Auto mode: tracks you drop go into a queue, and each one is mixed in on its
// own. The one playing gives way at its next breakdown (its outro if there's
// none left): the new track starts so its intro plays over that breakdown and
// its first drop lands where the breakdown ends, both in phase on the master
// clock. The crossfader brings the new track up over the breakdown and cuts
// to it on the drop; then the old deck stops and is free for the next.
class AutoDj : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    //! What it's doing, in a line.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    //! Titles waiting their turn.
    Q_PROPERTY(QStringList queue READ queueTitles NOTIFY queueChanged)
    //! A mix is planned or running.
    Q_PROPERTY(bool mixing READ mixing NOTIFY statusChanged)

public:
    AutoDj(AudioEngine *engine, Library *library, DeckController *a, DeckController *b,
           std::function<void(double)> setCrossfader, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString status() const { return m_status; }
    QStringList queueTitles() const;
    bool mixing() const { return m_plan.active; }

    //! A track for the queue (dropped or picked): mixed in when its turn comes.
    void take(const QString &trackId);
    Q_INVOKABLE void clearQueue();
    //! Called at display rate.
    void tick(double masterBeat);

signals:
    void enabledChanged();
    void statusChanged();
    void queueChanged();

private:
    struct Plan {
        bool active = false;
        bool started = false;     // the incoming deck is running
        int out = -1, in = -1;    // deck indices
        double startM = 0.0;      // master beat: the incoming deck starts
        double swapM = 0.0;       // master beat: its first drop, the old one cut
        double fadeEndM = 0.0;    // the incoming deck fully up (both playing) by here
        double inStart = 0.0, inDrop = 0.0; // on the incoming grid
        QString inId;
    };

    DeckController *deck(int i) const { return i == 0 ? m_a : m_b; }
    int onAir() const; // the deck playing (the one it started, while that's starting up), -1 none
    void next();               // load the next queued track where it can go
    bool plan(int out, int in); // false: not yet possible (still analysing)
    void finish();
    void cancel(const QString &why);
    void setStatus(const QString &s);
    void setX(double x);       // the crossfader: -1 all A .. 1 all B

    AudioEngine *m_engine;
    Library *m_library;
    DeckController *m_a, *m_b;
    std::function<void(double)> m_setCrossfader;

    bool m_enabled = false;
    QString m_status;
    QStringList m_queue;      // track ids
    int m_waiting = -1;       // a deck loaded with the next track, its mix not planned yet
    QString m_waitingId;
    bool m_startFirst = false; // nothing was playing: start the waiting deck as it is
    int m_current = -1;        // the deck it put on air
    int m_starting = -1;       // a deck told to play that doesn't say so yet
    Plan m_plan;
    double m_masterBeat = 0.0;
};
