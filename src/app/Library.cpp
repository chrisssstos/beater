#include "app/Library.h"

#include "analysis/KeyEstimator.h"
#include "analysis/StructureEstimator.h"
#include "analysis/TempoEstimator.h"
#include "audio/Decoder.h"
#include "core/Key.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString contentId(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    const qint64 size = f.size();
    h.addData(QByteArray::number(size));
    const qint64 chunk = 256 * 1024;
    h.addData(f.read(chunk));
    if (size > 2 * chunk) {
        f.seek(size - chunk);
        h.addData(f.read(chunk));
    }
    return QString::fromLatin1(h.result().toHex().left(20));
}

QString fmtTime(double s)
{
    if (s <= 0)
        return QString();
    const int t = int(s + 0.5);
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

Library::Library(QObject *parent)
    : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(2);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Library::save);
    m_camelot = QSettings().value("camelot", false).toBool();
    load();
    for (const QString &id : std::as_const(m_all))
        if (m_tracks[id].needsAnalysis())
            analyze(id);
}

Library::~Library()
{
    m_pool.clear();
    m_pool.waitForDone();
    if (m_saveTimer.isActive())
        save();
}

void Library::setCamelot(bool on)
{
    if (on == m_camelot)
        return;
    m_camelot = on;
    QSettings().setValue("camelot", on);
    if (!shown().isEmpty())
        emit dataChanged(index(0), index(int(shown().size()) - 1), {KeyTextRole});
    emit camelotChanged();
}

QString Library::keyText(int key) const
{
    return keys::valid(key) ? QString::fromStdString(keys::display(key, m_camelot)) : QString();
}

QStringList Library::audioExtensions()
{
    return {"mp3", "wav", "aif", "aiff", "flac", "m4a", "aac", "alac", "ogg"};
}

int Library::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(shown().size());
}

QVariant Library::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= shown().size())
        return {};
    const TrackRecord &r = m_tracks[shown()[index.row()]];
    switch (role) {
    case IdRole: return r.id;
    case TitleRole: return r.title;
    case PathRole: return r.path;
    case BpmRole: return r.bpm;
    case BpmTextRole: return r.bpm > 0 ? QString::number(r.bpm, 'f', r.bpm == std::round(r.bpm) ? 0 : 2) : QString();
    case KeyTextRole: return keyText(r.key);
    case DurationTextRole: return fmtTime(r.durationSec);
    case StatusRole: return r.status;
    case GridManualRole: return r.gridManual;
    }
    return {};
}

QHash<int, QByteArray> Library::roleNames() const
{
    return {
        {IdRole, "trackId"},
        {TitleRole, "title"},
        {PathRole, "path"},
        {BpmRole, "bpm"},
        {BpmTextRole, "bpmText"},
        {KeyTextRole, "keyText"},
        {DurationTextRole, "durationText"},
        {StatusRole, "status"},
        {GridManualRole, "gridManual"},
    };
}

QStringList Library::addUrls(const QList<QUrl> &urls)
{
    QStringList ids;
    const QStringList exts = audioExtensions();
    auto add = [&](const QString &f) {
        const QString id = addFile(f);
        if (!id.isEmpty() && !ids.contains(id))
            ids << id;
    };
    for (const QUrl &u : urls) {
        const QString p = u.isLocalFile() ? u.toLocalFile() : u.toString();
        QFileInfo fi(p);
        if (fi.isDir()) {
            QStringList files;
            QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (exts.contains(QFileInfo(f).suffix().toLower()))
                    files << f;
            }
            files.sort(Qt::CaseInsensitive);
            for (const QString &f : files)
                add(f);
        } else if (exts.contains(fi.suffix().toLower())) {
            add(fi.absoluteFilePath());
        }
    }
    return ids;
}

// Into the library (if new) and the playlist in view (if one is).
QString Library::addFile(const QString &path)
{
    const QString id = contentId(path);
    if (id.isEmpty())
        return {};
    TrackRecord &r = m_tracks[id];
    if (r.id.isEmpty()) {
        r.id = id;
        r.title = QFileInfo(path).completeBaseName();
    }
    r.path = path; // follows renames/moves
    auto append = [&](QList<QString> &list, bool visible) {
        if (list.contains(id))
            return;
        if (visible)
            beginInsertRows({}, int(list.size()), int(list.size()));
        list.append(id);
        if (visible)
            endInsertRows();
    };
    append(m_all, m_view < 0);
    if (m_view >= 0)
        append(m_lists[m_view].ids, true);
    emit countChanged();
    if (r.needsAnalysis())
        analyze(id);
    scheduleSave();
    return id;
}

void Library::removeAt(int row)
{
    if (row < 0 || row >= shown().size())
        return;
    const QString id = shown()[row];
    beginRemoveRows({}, row, row);
    shown().removeAt(row);
    endRemoveRows();
    // out of the library: out of its playlists too
    if (m_view < 0)
        for (Playlist &pl : m_lists)
            pl.ids.removeAll(id);
    emit countChanged();
    scheduleSave();
}

void Library::move(int from, int to)
{
    QList<QString> &list = shown();
    if (from == to || from < 0 || to < 0 || from >= list.size() || to >= list.size())
        return;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    list.move(from, to);
    endMoveRows();
    scheduleSave();
}

void Library::setView(int v)
{
    if (v < -1 || v >= m_lists.size())
        v = -1;
    if (v == m_view)
        return;
    beginResetModel();
    m_view = v;
    endResetModel();
    if (v >= 0)
        m_target = v;
    emit viewChanged();
    emit countChanged();
    scheduleSave();
}

QStringList Library::playlistNames() const
{
    QStringList l;
    for (const Playlist &pl : m_lists)
        l << pl.name;
    return l;
}

int Library::newPlaylist(const QString &name)
{
    m_lists.append({name.trimmed().isEmpty() ? QStringLiteral("Playlist %1").arg(m_lists.size() + 1) : name.trimmed(), {}});
    emit playlistsChanged();
    setView(int(m_lists.size()) - 1);
    scheduleSave();
    return m_view;
}

void Library::renamePlaylist(int i, const QString &name)
{
    if (i < 0 || i >= m_lists.size() || name.trimmed().isEmpty() || m_lists[i].name == name.trimmed())
        return;
    m_lists[i].name = name.trimmed();
    emit playlistsChanged();
    scheduleSave();
}

void Library::deletePlaylist(int i)
{
    if (i < 0 || i >= m_lists.size())
        return;
    if (m_view == i)
        setView(-1);
    m_lists.removeAt(i);
    if (m_view > i)
        --m_view;
    m_target = m_target == i ? -1 : m_target > i ? m_target - 1 : m_target;
    emit playlistsChanged();
    emit viewChanged();
    scheduleSave();
}

void Library::addToPlaylist(int row, int playlist)
{
    const QString id = idAt(row);
    if (id.isEmpty() || playlist < 0 || playlist >= m_lists.size() || m_lists[playlist].ids.contains(id))
        return;
    const bool visible = playlist == m_view;
    QList<QString> &ids = m_lists[playlist].ids;
    if (visible)
        beginInsertRows({}, int(ids.size()), int(ids.size()));
    ids.append(id);
    if (visible)
        endInsertRows();
    emit countChanged();
    scheduleSave();
}

void Library::reanalyze(int row)
{
    const QString id = idAt(row);
    if (id.isEmpty())
        return;
    analyze(id);
}

void Library::autoDetect(const QString &id, bool grid, bool key)
{
    if (!m_tracks.contains(id))
        return;
    TrackRecord r = m_tracks[id];
    if (grid)
        r.gridManual = false;
    if (key)
        r.keyManual = false;
    update(r);
    analyze(id);
}

QString Library::idAt(int row) const
{
    return row >= 0 && row < shown().size() ? shown()[row] : QString();
}

int Library::rowOf(const QString &id) const
{
    return int(shown().indexOf(id));
}

void Library::update(const TrackRecord &rec)
{
    if (!m_tracks.contains(rec.id))
        return;
    m_tracks[rec.id] = rec;
    const int row = rowOf(rec.id);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit recordChanged(rec.id);
    scheduleSave();
}

void Library::analyze(const QString &id)
{
    TrackRecord &r = m_tracks[id];
    if (r.status == QLatin1String("analyzing"))
        return;
    r.status = QStringLiteral("analyzing");
    ++m_analyzing;
    emit analyzingCountChanged();
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit recordChanged(id);

    const QString path = r.path;
    // sections follow the grid the track will play on: yours if you set one
    const bool manualGrid = r.gridManual && r.bpm > 0.0;
    const double gridBpm = r.bpm, gridFirst = r.firstBeatSec;
    m_pool.start([this, id, path, manualGrid, gridBpm, gridFirst] {
        constexpr uint32_t kSr = 22050;
        std::vector<float> mono;
        std::string err;
        TempoResult tr;
        KeyResult kr;
        StructureResult sr;
        double duration = 0.0;
        const bool ok = decoder::decodeMono(path.toStdString(), kSr, mono, &err);
        if (ok) {
            duration = double(mono.size()) / kSr;
            tr = estimateTempo(mono, kSr);
            kr = estimateKey(mono, kSr);
            if (manualGrid)
                sr = estimateStructure(mono, kSr, gridBpm, gridFirst);
            else if (tr.ok)
                sr = estimateStructure(mono, kSr, tr.bpm, tr.firstBeatSec);
        }
        QMetaObject::invokeMethod(this, [this, id, ok, err, tr, kr, sr, duration] {
            --m_analyzing;
            emit analyzingCountChanged();
            if (!m_tracks.contains(id))
                return;
            TrackRecord r = m_tracks[id];
            r.status.clear();
            if (!ok) {
                r.status = QStringLiteral("error: ") + QString::fromStdString(err);
            } else {
                r.durationSec = duration;
                r.analyzed = true;
                r.analysisVersion = kAnalysisVersion;
                if (!r.gridManual && tr.ok) {
                    r.bpm = tr.bpm;
                    r.firstBeatSec = tr.firstBeatSec;
                    r.gridConfidence = tr.confidence;
                }
                if (!r.keyManual && kr.ok) {
                    r.key = kr.key;
                    r.keyConfidence = kr.confidence;
                }
                r.sections.clear();
                r.firstDropSec = -1.0;
                if (sr.ok) {
                    for (const Section &s : sr.sections)
                        r.sections.append({s.startSec, s.endSec, s.drop});
                    r.firstDropSec = sr.firstDropSec;
                }
            }
            update(r);
        }, Qt::QueuedConnection);
    });
}

QString Library::storePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/library.json");
}

void Library::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject tracks = root["tracks"].toObject();
    for (auto it = tracks.begin(); it != tracks.end(); ++it)
        m_tracks.insert(it.key(), TrackRecord::fromJson(it.key(), it.value().toObject()));
    for (const QJsonValue &v : root["playlist"].toArray()) { // the library ("playlist" before playlists)
        const QString id = v.toString();
        if (m_tracks.contains(id) && !m_all.contains(id))
            m_all.append(id);
    }
    for (const QJsonValue &v : root["playlists"].toArray()) {
        const QJsonObject o = v.toObject();
        Playlist pl{o["name"].toString(), {}};
        for (const QJsonValue &t : o["tracks"].toArray())
            if (m_tracks.contains(t.toString()) && !pl.ids.contains(t.toString()))
                pl.ids.append(t.toString());
        m_lists.append(pl);
    }
    const int v = root["view"].toInt(-1);
    m_view = v >= 0 && v < m_lists.size() ? v : -1;
    const int t = root["target"].toInt(-1);
    m_target = t >= 0 && t < m_lists.size() ? t : -1;
}

void Library::scheduleSave()
{
    m_saveTimer.start();
}

void Library::save()
{
    QJsonObject tracks;
    for (auto it = m_tracks.cbegin(); it != m_tracks.cend(); ++it)
        tracks.insert(it.key(), it.value().toJson());
    QJsonArray playlist;
    for (const QString &id : std::as_const(m_all))
        playlist.append(id);
    QJsonArray playlists;
    for (const Playlist &pl : std::as_const(m_lists)) {
        QJsonArray ids;
        for (const QString &id : pl.ids)
            ids.append(id);
        playlists.append(QJsonObject{{"name", pl.name}, {"tracks", ids}});
    }
    QJsonObject root;
    root["version"] = 2;
    root["tracks"] = tracks;
    root["playlist"] = playlist; // the library, all tracks
    root["playlists"] = playlists;
    root["view"] = m_view;
    root["target"] = m_target;
    QSaveFile f(storePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}
