#pragma once
#include "core/BeatGrid.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

// Bump when the analyzers change, so tracks analyzed before get analyzed again
// (manual edits are kept either way).
constexpr int kAnalysisVersion = 7; // 2: key detection, 3: fine beat phase + grid from the start, 4: minor / bass key, 5: learned key profiles, 6: sections, 7: bar-length tempo, rise-edge beats, learned downbeat
// A stretch of the track: a drop (the full low end) or not (intro, breakdown, outro).
struct TrackSection {
    double startSec = 0.0;
    double endSec = 0.0;
    bool drop = false;
    bool operator==(const TrackSection &o) const { return startSec == o.startSec && endSec == o.endSec && drop == o.drop; }
};

// Everything the app remembers about a track. Keyed by a content hash so edits
// survive renames and moves.
struct TrackRecord {
    QString id;
    QString path;
    QString title;
    double durationSec = 0.0;

    double bpm = 0.0;          // 0 = unknown
    double firstBeatSec = 0.0; // a downbeat
    bool gridManual = false;   // user edited: analysis never overwrites
    double gridConfidence = 0.0;

    int key = -1;              // 0..23, see core/Key.h
    bool keyManual = false;    // user edited: analysis never overwrites
    double keyConfidence = 0.0;

    // Loop region in beats. end <= start = automatic end loop.
    double loopStartBeat = 0.0;
    double loopEndBeat = 0.0;
    // Intro loop in beats. end <= start = automatic (the first N bars).
    double introStartBeat = 0.0;
    double introEndBeat = 0.0;
    // In / Out loop in beats. end <= start = none.
    double customStartBeat = 0.0;
    double customEndBeat = 0.0;

    // Sections (detected), in seconds so grid edits don't move them.
    QVector<TrackSection> sections;
    double firstDropSec = -1.0; // the end of the intro (< 0: unknown)

    bool analyzed = false;
    int analysisVersion = 0;
    QString status;            // transient: "analyzing", "error: ..."

    BeatGrid grid() const { return BeatGrid{bpm, firstBeatSec}; }
    bool hasManualLoop() const { return loopEndBeat > loopStartBeat; }
    bool hasManualIntro() const { return introEndBeat > introStartBeat; }
    bool hasCustomLoop() const { return customEndBeat > customStartBeat; }
    bool needsAnalysis() const { return !analyzed || analysisVersion < kAnalysisVersion; }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o["path"] = path;
        o["title"] = title;
        o["duration"] = durationSec;
        o["bpm"] = bpm;
        o["firstBeat"] = firstBeatSec;
        o["gridManual"] = gridManual;
        o["gridConfidence"] = gridConfidence;
        o["key"] = key;
        o["keyManual"] = keyManual;
        o["keyConfidence"] = keyConfidence;
        o["loopStart"] = loopStartBeat;
        o["loopEnd"] = loopEndBeat;
        o["introStart"] = introStartBeat;
        o["introEnd"] = introEndBeat;
        o["customStart"] = customStartBeat;
        o["customEnd"] = customEndBeat;
        QJsonArray secs;
        for (const TrackSection &s : sections)
            secs.append(QJsonArray{s.startSec, s.endSec, s.drop});
        o["sections"] = secs;
        o["firstDrop"] = firstDropSec;
        o["analyzed"] = analyzed;
        o["analysisVersion"] = analysisVersion;
        return o;
    }

    static TrackRecord fromJson(const QString &id, const QJsonObject &o)
    {
        TrackRecord r;
        r.id = id;
        r.path = o["path"].toString();
        r.title = o["title"].toString();
        r.durationSec = o["duration"].toDouble();
        r.bpm = o["bpm"].toDouble();
        r.firstBeatSec = o["firstBeat"].toDouble();
        r.gridManual = o["gridManual"].toBool();
        r.gridConfidence = o["gridConfidence"].toDouble();
        r.key = o["key"].toInt(-1);
        r.keyManual = o["keyManual"].toBool();
        r.keyConfidence = o["keyConfidence"].toDouble();
        r.loopStartBeat = o["loopStart"].toDouble();
        r.loopEndBeat = o["loopEnd"].toDouble();
        r.introStartBeat = o["introStart"].toDouble();
        r.introEndBeat = o["introEnd"].toDouble();
        r.customStartBeat = o["customStart"].toDouble();
        r.customEndBeat = o["customEnd"].toDouble();
        for (const QJsonValue &v : o["sections"].toArray()) {
            const QJsonArray a = v.toArray();
            if (a.size() == 3)
                r.sections.append({a[0].toDouble(), a[1].toDouble(), a[2].toBool()});
        }
        r.firstDropSec = o["firstDrop"].toDouble(-1.0);
        r.analyzed = o["analyzed"].toBool();
        r.analysisVersion = o["analysisVersion"].toInt();
        return r;
    }
};
