#include "Database.h"

#include <QDateTime>
#include <algorithm>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

#include "Matcher.h"

static const char *kConn = "orpheus";
static const char *kCacheName = "Cache local";

static const char *kTrackCols =
    "id, yt_id, title, artist, album, duration, thumbnail, file_path, codec, bitrate, "
    "file_size, state, error, spotify_id, play_count, last_played, added_at";

qint64 nowSecs()
{
    return QDateTime::currentSecsSinceEpoch();
}

QVariantMap Playlist::toMap() const
{
    return {{"id", id},
            {"name", name},
            {"kind", kind},
            {"sourceRef", sourceRef},
            {"trackCount", trackCount},
            {"readyCount", readyCount},
            {"totalDuration", totalDuration},
            {"durationText", formatTime(totalDuration)}};
}

QVariantMap ImportJob::toMap() const
{
    return {{"id", id},         {"source", source}, {"name", name},     {"playlistId", playlistId},
            {"total", total},   {"done", done},     {"failed", failed}, {"status", status},
            {"message", message}};
}

namespace {

QSqlQuery exec(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec())
        qWarning() << "SQL:" << q.lastError().text() << sql.left(120);
    return q;
}

// Les chaînes vides sont stockées en NULL (compatibilité avec le schéma existant).
QVariant nullable(const QString &s)
{
    return s.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(s);
}

QVariant nullable(double d)
{
    return d > 0 ? QVariant(d) : QVariant(QMetaType(QMetaType::Double));
}

Track trackFromQuery(const QSqlQuery &q)
{
    Track t;
    t.id = q.value(0).toLongLong();
    t.ytId = q.value(1).toString();
    t.title = q.value(2).toString();
    t.artist = q.value(3).toString();
    t.album = q.value(4).toString();
    t.duration = q.value(5).toDouble();
    t.thumbnail = q.value(6).toString();
    t.filePath = q.value(7).toString();
    t.codec = q.value(8).toString();
    t.bitrate = q.value(9).toDouble();
    t.fileSize = q.value(10).toLongLong();
    t.state = q.value(11).toString();
    t.error = q.value(12).toString();
    t.spotifyId = q.value(13).toString();
    t.playCount = q.value(14).toLongLong();
    t.lastPlayed = q.value(15).toLongLong();
    t.addedAt = q.value(16).toLongLong();
    return t;
}

const char *kSchema[] = {
    "PRAGMA journal_mode = WAL",
    "PRAGMA foreign_keys = ON",
    "CREATE TABLE IF NOT EXISTS tracks ("
    " id INTEGER PRIMARY KEY, yt_id TEXT NOT NULL UNIQUE, title TEXT NOT NULL, artist TEXT NOT NULL DEFAULT '',"
    " album TEXT, duration REAL, thumbnail TEXT, file_path TEXT, codec TEXT, bitrate REAL, file_size INTEGER,"
    " state TEXT NOT NULL DEFAULT 'remote', error TEXT, spotify_id TEXT, match_key TEXT,"
    " play_count INTEGER NOT NULL DEFAULT 0, last_played INTEGER, added_at INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS tracks_spotify ON tracks(spotify_id)",
    "CREATE INDEX IF NOT EXISTS tracks_match_key ON tracks(match_key)",
    "CREATE INDEX IF NOT EXISTS tracks_state ON tracks(state)",
    "CREATE TABLE IF NOT EXISTS playlists ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL, source_ref TEXT, created_at INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS playlist_tracks ("
    " playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
    " track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
    " position INTEGER NOT NULL, added_at INTEGER NOT NULL, PRIMARY KEY (playlist_id, track_id))",
    "CREATE INDEX IF NOT EXISTS playlist_tracks_track ON playlist_tracks(track_id)",
    "CREATE TABLE IF NOT EXISTS match_cache (key TEXT PRIMARY KEY, yt_id TEXT NOT NULL, created_at INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
    "CREATE TABLE IF NOT EXISTS import_jobs ("
    " id INTEGER PRIMARY KEY, source TEXT NOT NULL, name TEXT NOT NULL, playlist_id INTEGER,"
    " total INTEGER NOT NULL DEFAULT 0, done INTEGER NOT NULL DEFAULT 0, failed INTEGER NOT NULL DEFAULT 0,"
    " status TEXT NOT NULL, message TEXT, created_at INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS lyrics ("
    " track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE, status TEXT NOT NULL,"
    " synced TEXT, plain TEXT, instrumental INTEGER NOT NULL DEFAULT 0, fetched_at INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS plays ("
    " id INTEGER PRIMARY KEY, track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,"
    " played_at INTEGER NOT NULL, listened REAL NOT NULL)",
    "CREATE INDEX IF NOT EXISTS plays_time ON plays(played_at)",
};

}  // namespace

QSqlDatabase Database::db() const
{
    return QSqlDatabase::database(kConn);
}

bool Database::open(const QString &path, QString *error)
{
    QSqlDatabase d = QSqlDatabase::contains(kConn) ? QSqlDatabase::database(kConn)
                                                   : QSqlDatabase::addDatabase("QSQLITE", kConn);
    d.setDatabaseName(path);
    d.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (!d.open()) {
        if (error)
            *error = d.lastError().text();
        return false;
    }
    for (const char *sql : kSchema)
        exec(d, sql);
    // Un téléchargement interrompu par un arrêt brutal sera relancé ; les échecs aussi
    // (souvent des refus temporaires de YouTube).
    exec(d, "UPDATE tracks SET state = 'queued', error = NULL WHERE state IN ('downloading', 'failed')");
    exec(d, "UPDATE import_jobs SET status = 'interrupted', message = 'Interrompu (application fermée)' "
            "WHERE status IN ('running', 'pending')");
    QSqlQuery q = exec(d, "SELECT id FROM playlists WHERE kind = 'cache'");
    if (q.next()) {
        m_cachePlaylist = q.value(0).toLongLong();
    } else {
        QSqlQuery ins = exec(d, "INSERT INTO playlists(name, kind, created_at) VALUES (?, 'cache', ?)",
                             {kCacheName, nowSecs()});
        m_cachePlaylist = ins.lastInsertId().toLongLong();
    }
    return true;
}

// ---------- Réglages ----------

QString Database::setting(const QString &key, const QString &def) const
{
    QSqlQuery q = exec(db(), "SELECT value FROM settings WHERE key = ?", {key});
    return q.next() ? q.value(0).toString() : def;
}

bool Database::settingBool(const QString &key, bool def) const
{
    const QString v = setting(key);
    return v.isEmpty() ? def : v == QLatin1String("true");
}

void Database::setSetting(const QString &key, const QString &value)
{
    exec(db(), "INSERT INTO settings(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
         {key, value});
}

void Database::removeSetting(const QString &key)
{
    exec(db(), "DELETE FROM settings WHERE key = ?", {key});
}

// ---------- Morceaux ----------

QList<Track> Database::queryTracks(const QString &sql, const QVariantList &binds) const
{
    QSqlQuery q = exec(db(), sql, binds);
    QList<Track> out;
    while (q.next())
        out.append(trackFromQuery(q));
    return out;
}

Track Database::upsertTrack(const Track &t)
{
    const QString key = Matcher::matchKey(t.artist, t.title);
    // Les métadonnées Spotify, plus propres que les titres YouTube, priment.
    exec(db(),
         "INSERT INTO tracks(yt_id, title, artist, album, duration, thumbnail, spotify_id, match_key, added_at) "
         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
         "ON CONFLICT(yt_id) DO UPDATE SET "
         " title = CASE WHEN excluded.spotify_id IS NOT NULL OR tracks.title = '' OR tracks.title = tracks.yt_id "
         "   THEN excluded.title ELSE tracks.title END,"
         " artist = CASE WHEN excluded.spotify_id IS NOT NULL OR tracks.artist = '' THEN excluded.artist ELSE tracks.artist END,"
         " album = COALESCE(excluded.album, tracks.album),"
         " duration = COALESCE(tracks.duration, excluded.duration),"
         " thumbnail = COALESCE(tracks.thumbnail, excluded.thumbnail),"
         " spotify_id = COALESCE(excluded.spotify_id, tracks.spotify_id),"
         " match_key = CASE WHEN excluded.spotify_id IS NOT NULL OR tracks.match_key IS NULL "
         "   THEN excluded.match_key ELSE tracks.match_key END",
         {t.ytId, t.title.isEmpty() ? t.ytId : t.title, QVariant(t.artist.isEmpty() ? QStringLiteral("") : t.artist), nullable(t.album), nullable(t.duration),
          nullable(t.thumbnail), nullable(t.spotifyId), nullable(key), nowSecs()});
    return trackByYt(t.ytId).value_or(t);
}

std::optional<Track> Database::track(qint64 id) const
{
    const auto v = queryTracks(QStringLiteral("SELECT %1 FROM tracks WHERE id = ?").arg(kTrackCols), {id});
    return v.isEmpty() ? std::nullopt : std::optional(v.first());
}

std::optional<Track> Database::trackByYt(const QString &ytId) const
{
    const auto v = queryTracks(QStringLiteral("SELECT %1 FROM tracks WHERE yt_id = ?").arg(kTrackCols), {ytId});
    return v.isEmpty() ? std::nullopt : std::optional(v.first());
}

std::optional<Track> Database::findLocalMatch(const QString &spotifyId, const QString &artist, const QString &title) const
{
    if (!spotifyId.isEmpty()) {
        const auto v = queryTracks(
            QStringLiteral("SELECT %1 FROM tracks WHERE spotify_id = ? ORDER BY state = 'ready' DESC LIMIT 1").arg(kTrackCols),
            {spotifyId});
        if (!v.isEmpty())
            return v.first();
    }
    const QString key = Matcher::matchKey(artist, title);
    if (key.isEmpty())
        return std::nullopt;
    const auto v = queryTracks(
        QStringLiteral("SELECT %1 FROM tracks WHERE match_key = ? ORDER BY state = 'ready' DESC LIMIT 1").arg(kTrackCols),
        {key});
    if (!v.isEmpty())
        return v.first();
    QSqlQuery q = exec(db(), "SELECT yt_id FROM match_cache WHERE key = ?", {key});
    if (q.next())
        return trackByYt(q.value(0).toString());
    return std::nullopt;
}

void Database::rememberMatch(const QString &artist, const QString &title, const QString &ytId)
{
    const QString key = Matcher::matchKey(artist, title);
    if (!key.isEmpty())
        exec(db(), "INSERT OR REPLACE INTO match_cache(key, yt_id, created_at) VALUES (?, ?, ?)", {key, ytId, nowSecs()});
}

void Database::setState(qint64 id, const QString &state, const QString &error)
{
    exec(db(), "UPDATE tracks SET state = ?, error = ? WHERE id = ?", {state, nullable(error), id});
}

void Database::setFile(qint64 id, const QString &path, const QString &codec, double bitrate, qint64 size, double duration)
{
    exec(db(),
         "UPDATE tracks SET file_path = ?, codec = ?, bitrate = ?, file_size = ?, state = 'ready', error = NULL, "
         "duration = COALESCE(?, duration) WHERE id = ?",
         {path, nullable(codec), nullable(bitrate), size, nullable(duration), id});
}

void Database::enrichMetadata(qint64 id, const QString &title, const QString &artist, const QString &album)
{
    exec(db(),
         "UPDATE tracks SET "
         " title = CASE WHEN spotify_id IS NULL AND ? IS NOT NULL THEN ? ELSE title END,"
         " artist = CASE WHEN spotify_id IS NULL AND ? IS NOT NULL THEN ? ELSE artist END,"
         " album = COALESCE(album, ?) WHERE id = ?",
         {nullable(title), nullable(title), nullable(artist), nullable(artist), nullable(album), id});
    if (const auto t = track(id); t && t->spotifyId.isEmpty())
        exec(db(), "UPDATE tracks SET match_key = ? WHERE id = ?", {Matcher::matchKey(t->artist, t->title), id});
}

void Database::clearFile(qint64 id)
{
    exec(db(), "UPDATE tracks SET file_path = NULL, codec = NULL, bitrate = NULL, file_size = NULL, state = 'remote' "
               "WHERE id = ?",
         {id});
}

void Database::markStarted(qint64 id)
{
    exec(db(), "UPDATE tracks SET last_played = ? WHERE id = ?", {nowSecs(), id});
    addToPlaylist(m_cachePlaylist, id);
}

qint64 Database::recordPlay(qint64 trackId, double listened)
{
    exec(db(), "UPDATE tracks SET play_count = play_count + 1 WHERE id = ?", {trackId});
    QSqlQuery q = exec(db(), "INSERT INTO plays(track_id, played_at, listened) VALUES (?, ?, ?)", {trackId, nowSecs(), listened});
    return q.lastInsertId().toLongLong();
}

void Database::updatePlay(qint64 playId, double listened)
{
    exec(db(), "UPDATE plays SET listened = ? WHERE id = ?", {listened, playId});
}

ListenStats Database::listenStats(qint64 since) const
{
    ListenStats st;
    QSqlQuery q = exec(db(), "SELECT COALESCE(SUM(listened), 0), COUNT(*), COUNT(DISTINCT track_id) FROM plays WHERE played_at >= ?",
                       {since});
    if (q.next()) {
        st.seconds = q.value(0).toDouble();
        st.plays = q.value(1).toInt();
        st.tracks = q.value(2).toInt();
    }
    return st;
}

QList<TopTrack> Database::topTracks(qint64 since, int limit) const
{
    QStringList cols;
    for (const QString &c : QString(kTrackCols).split(", "))
        cols << "t." + c;
    QSqlQuery q = exec(db(),
                       QStringLiteral("SELECT %1, COUNT(*), SUM(p.listened) FROM plays p JOIN tracks t ON t.id = p.track_id "
                                      "WHERE p.played_at >= ? GROUP BY t.id ORDER BY COUNT(*) DESC, SUM(p.listened) DESC LIMIT ?")
                           .arg(cols.join(", ")),
                       {since, limit});
    const int n = int(cols.size());
    QList<TopTrack> out;
    while (q.next())
        out.append({trackFromQuery(q), q.value(n).toInt(), q.value(n + 1).toDouble()});
    return out;
}

QList<TopArtist> Database::topArtists(qint64 since, int limit, int *distinct) const
{
    QSqlQuery q = exec(db(),
                       "SELECT t.artist, t.title, COUNT(*), SUM(p.listened) FROM plays p JOIN tracks t ON t.id = p.track_id "
                       "WHERE p.played_at >= ? GROUP BY t.id",
                       {since});
    // Regroupement par artiste principal, sans tenir compte de la casse ni des accents
    // (« VALD », « Vald feat. Angèle » → Vald).
    QHash<QString, TopArtist> groups;
    QHash<QString, QHash<QString, int>> spellings;
    while (q.next()) {
        QString artist = q.value(0).toString();
        if (artist.trimmed().isEmpty())
            artist = Matcher::splitYoutubeTitle(q.value(1).toString(), {}).first;
        const QString name = Matcher::primaryArtist(artist);
        const QString key = Matcher::normalize(name);
        if (key.isEmpty())
            continue;
        TopArtist &g = groups[key];
        g.plays += q.value(2).toInt();
        g.seconds += q.value(3).toDouble();
        ++g.tracks;
        spellings[key][name] += q.value(2).toInt();
    }
    QList<TopArtist> out;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        // Graphie la plus écoutée pour l'affichage.
        const auto &sp = spellings[it.key()];
        int bestCount = -1;
        for (auto s = sp.cbegin(); s != sp.cend(); ++s)
            if (s.value() > bestCount) {
                bestCount = s.value();
                it->name = s.key();
            }
        out.append(*it);
    }
    std::sort(out.begin(), out.end(), [](const TopArtist &a, const TopArtist &b) {
        return a.plays != b.plays ? a.plays > b.plays : a.seconds > b.seconds;
    });
    if (distinct)
        *distinct = int(out.size());
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

std::optional<StoredLyrics> Database::lyrics(qint64 trackId) const
{
    QSqlQuery q = exec(db(), "SELECT status, synced, plain, instrumental, fetched_at FROM lyrics WHERE track_id = ?", {trackId});
    if (!q.next())
        return std::nullopt;
    StoredLyrics l;
    l.status = q.value(0).toString();
    l.synced = q.value(1).toString();
    l.plain = q.value(2).toString();
    l.instrumental = q.value(3).toBool();
    l.fetchedAt = q.value(4).toLongLong();
    return l;
}

void Database::saveLyrics(qint64 trackId, const StoredLyrics &l)
{
    exec(db(),
         "INSERT OR REPLACE INTO lyrics(track_id, status, synced, plain, instrumental, fetched_at) VALUES (?, ?, ?, ?, ?, ?)",
         {trackId, l.status, nullable(l.synced), nullable(l.plain), l.instrumental ? 1 : 0, nowSecs()});
}

QList<Track> Database::tracksNeedingLyrics(int limit) const
{
    QStringList cols;
    for (const QString &c : QString(kTrackCols).split(", "))
        cols << "t." + c;
    return queryTracks(QStringLiteral("SELECT %1 FROM tracks t LEFT JOIN lyrics l ON l.track_id = t.id "
                                      "WHERE t.state = 'ready' AND (l.track_id IS NULL OR (l.status = 'missing' AND l.fetched_at < ?)) "
                                      "ORDER BY COALESCE(t.last_played, t.added_at) DESC LIMIT ?")
                           .arg(cols.join(", ")),
                       {nowSecs() - 7 * 86400, limit});
}

QList<Track> Database::tracksInState(const QString &state) const
{
    return queryTracks(QStringLiteral("SELECT %1 FROM tracks WHERE state = ? ORDER BY id").arg(kTrackCols), {state});
}

QList<Track> Database::searchLocal(const QString &query, int limit) const
{
    // Chaque mot doit apparaître dans le titre, l'artiste ou l'album (dans n'importe quel ordre).
    QStringList conds;
    QVariantList binds;
    for (QString w : query.split(' ', Qt::SkipEmptyParts)) {
        w.remove('%').remove('_');
        if (w.isEmpty())
            continue;
        conds << "(title || ' ' || artist || ' ' || COALESCE(album, '')) LIKE ?";
        binds << '%' + w + '%';
    }
    if (conds.isEmpty())
        return {};
    binds << limit;
    return queryTracks(QStringLiteral("SELECT %1 FROM tracks WHERE state = 'ready' AND %2 ORDER BY play_count DESC LIMIT ?")
                           .arg(kTrackCols, conds.join(" AND ")),
                       binds);
}

// ---------- Cache ----------

QPair<qint64, int> Database::cacheSize() const
{
    QSqlQuery q = exec(db(), "SELECT COALESCE(SUM(file_size), 0), COUNT(*) FROM tracks WHERE state = 'ready'");
    return q.next() ? QPair<qint64, int>(q.value(0).toLongLong(), q.value(1).toInt()) : QPair<qint64, int>(0, 0);
}

QList<Track> Database::evictionCandidates() const
{
    return queryTracks(QStringLiteral("SELECT %1 FROM tracks t WHERE state = 'ready' AND NOT EXISTS ("
                                      " SELECT 1 FROM playlist_tracks pt JOIN playlists p ON p.id = pt.playlist_id"
                                      " WHERE pt.track_id = t.id AND p.kind != 'cache') "
                                      "ORDER BY COALESCE(last_played, added_at) ASC")
                           .arg(kTrackCols));
}

bool Database::isPinned(qint64 trackId) const
{
    QSqlQuery q = exec(db(),
                       "SELECT EXISTS(SELECT 1 FROM playlist_tracks pt JOIN playlists p ON p.id = pt.playlist_id "
                       "WHERE pt.track_id = ? AND p.kind != 'cache')",
                       {trackId});
    return q.next() && q.value(0).toBool();
}

// ---------- Playlists ----------

QList<Playlist> Database::playlists() const
{
    QSqlQuery q = exec(db(),
                       "SELECT p.id, p.name, p.kind, p.source_ref, p.created_at, COUNT(t.id), "
                       " COALESCE(SUM(t.state = 'ready'), 0), COALESCE(SUM(t.duration), 0) "
                       "FROM playlists p LEFT JOIN playlist_tracks pt ON pt.playlist_id = p.id "
                       "LEFT JOIN tracks t ON t.id = pt.track_id "
                       "GROUP BY p.id ORDER BY p.kind != 'cache', p.name COLLATE NOCASE");
    QList<Playlist> out;
    while (q.next()) {
        Playlist p;
        p.id = q.value(0).toLongLong();
        p.name = q.value(1).toString();
        p.kind = q.value(2).toString();
        p.sourceRef = q.value(3).toString();
        p.createdAt = q.value(4).toLongLong();
        p.trackCount = q.value(5).toInt();
        p.readyCount = q.value(6).toInt();
        p.totalDuration = q.value(7).toDouble();
        out.append(p);
    }
    return out;
}

std::optional<Playlist> Database::playlist(qint64 id) const
{
    for (const Playlist &p : playlists())
        if (p.id == id)
            return p;
    return std::nullopt;
}

qint64 Database::createPlaylist(const QString &name, const QString &kind, const QString &sourceRef)
{
    QSqlQuery q = exec(db(), "INSERT INTO playlists(name, kind, source_ref, created_at) VALUES (?, ?, ?, ?)",
                       {name, kind, nullable(sourceRef), nowSecs()});
    return q.lastInsertId().toLongLong();
}

std::optional<qint64> Database::playlistBySource(const QString &kind, const QString &sourceRef) const
{
    QSqlQuery q = exec(db(), "SELECT id FROM playlists WHERE kind = ? AND source_ref = ?", {kind, sourceRef});
    return q.next() ? std::optional(q.value(0).toLongLong()) : std::nullopt;
}

void Database::renamePlaylist(qint64 id, const QString &name)
{
    exec(db(), "UPDATE playlists SET name = ? WHERE id = ?", {name, id});
}

bool Database::deletePlaylist(qint64 id)
{
    if (id == m_cachePlaylist)
        return false;
    exec(db(), "DELETE FROM playlists WHERE id = ?", {id});
    return true;
}

QList<Track> Database::playlistTracks(qint64 id) const
{
    QStringList cols;
    for (const QString &c : QString(kTrackCols).split(", "))
        cols << "t." + c;
    return queryTracks(QStringLiteral("SELECT %1 FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id "
                                      "WHERE pt.playlist_id = ? ORDER BY pt.position")
                           .arg(cols.join(", ")),
                       {id});
}

void Database::addToPlaylist(qint64 playlistId, qint64 trackId)
{
    exec(db(),
         "INSERT OR IGNORE INTO playlist_tracks(playlist_id, track_id, position, added_at) "
         "VALUES (?, ?, (SELECT COALESCE(MAX(position), 0) + 1 FROM playlist_tracks WHERE playlist_id = ?), ?)",
         {playlistId, trackId, playlistId, nowSecs()});
}

QHash<qint64, bool> Database::playlistsContaining(qint64 trackId, const QString &matchKey) const
{
    QHash<qint64, bool> out;
    if (trackId <= 0 && matchKey.isEmpty())
        return out;
    QSqlQuery q = exec(db(),
                       "SELECT pt.playlist_id, MAX(t.id = ?) FROM playlist_tracks pt JOIN tracks t ON t.id = pt.track_id "
                       "WHERE t.id = ? OR (? != '' AND t.match_key = ?) GROUP BY pt.playlist_id",
                       {trackId, trackId, matchKey, matchKey});
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toBool());
    return out;
}

void Database::removeFromPlaylist(qint64 playlistId, qint64 trackId)
{
    exec(db(), "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?", {playlistId, trackId});
}

void Database::moveInPlaylist(qint64 playlistId, qint64 trackId, int newIndex)
{
    QList<qint64> ids;
    for (const Track &t : playlistTracks(playlistId))
        ids.append(t.id);
    const qsizetype old = ids.indexOf(trackId);
    if (old < 0)
        return;
    ids.removeAt(old);
    ids.insert(qBound(qsizetype(0), qsizetype(newIndex), ids.size()), trackId);
    QSqlDatabase d = db();
    d.transaction();
    for (int i = 0; i < ids.size(); ++i)
        exec(d, "UPDATE playlist_tracks SET position = ? WHERE playlist_id = ? AND track_id = ?", {i + 1, playlistId, ids[i]});
    d.commit();
}

// ---------- Imports ----------

qint64 Database::createJob(const QString &source, const QString &name)
{
    QSqlQuery q = exec(db(), "INSERT INTO import_jobs(source, name, status, created_at) VALUES (?, ?, 'pending', ?)",
                       {source, name, nowSecs()});
    return q.lastInsertId().toLongLong();
}

void Database::updateJob(const ImportJob &j)
{
    exec(db(),
         "UPDATE import_jobs SET name = ?, playlist_id = ?, total = ?, done = ?, failed = ?, status = ?, message = ? "
         "WHERE id = ?",
         {j.name, j.playlistId > 0 ? QVariant(j.playlistId) : QVariant(QMetaType(QMetaType::LongLong)), j.total, j.done,
          j.failed, j.status, nullable(j.message), j.id});
}

QList<ImportJob> Database::jobs() const
{
    QSqlQuery q = exec(db(), "SELECT id, source, name, playlist_id, total, done, failed, status, message, created_at "
                             "FROM import_jobs ORDER BY id DESC LIMIT 30");
    QList<ImportJob> out;
    while (q.next()) {
        ImportJob j;
        j.id = q.value(0).toLongLong();
        j.source = q.value(1).toString();
        j.name = q.value(2).toString();
        j.playlistId = q.value(3).toLongLong();
        j.total = q.value(4).toInt();
        j.done = q.value(5).toInt();
        j.failed = q.value(6).toInt();
        j.status = q.value(7).toString();
        j.message = q.value(8).toString();
        j.createdAt = q.value(9).toLongLong();
        out.append(j);
    }
    return out;
}

void Database::clearFinishedJobs()
{
    exec(db(), "DELETE FROM import_jobs WHERE status NOT IN ('running', 'pending')");
}
