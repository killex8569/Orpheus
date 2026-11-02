#pragma once

#include <QHash>

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>
#include <optional>

#include "Track.h"

struct Playlist {
    qint64 id = 0;
    QString name;
    QString kind;  // cache | user | youtube | spotify
    QString sourceRef;
    qint64 createdAt = 0;
    int trackCount = 0;
    int readyCount = 0;
    double totalDuration = 0;
    qint64 diskSize = 0;  // octets des morceaux de la playlist présents en local
    QVariantMap toMap() const;
};

struct ImportJob {
    qint64 id = 0;
    QString source;
    QString name;
    qint64 playlistId = 0;
    int total = 0, done = 0, failed = 0;
    QString status;  // pending | running | done | error | interrupted
    QString message;
    qint64 createdAt = 0;
    QVariantMap toMap() const;
};

// Paroles gardées en base (écoute hors ligne).
struct StoredLyrics {
    QString status;  // found | missing
    QString synced, plain;
    bool instrumental = false;
    qint64 fetchedAt = 0;
};

// Statistiques d'écoute sur une période.
struct ListenStats {
    double seconds = 0;
    int plays = 0;
    int tracks = 0;
};
struct TopTrack {
    Track track;
    int plays = 0;
    double seconds = 0;
};
struct TopArtist {
    QString name;
    int plays = 0;
    double seconds = 0;
    int tracks = 0;
};

qint64 nowSecs();

// Accès SQLite (thread principal uniquement). Schéma compatible avec la version précédente.
class Database {
public:
    bool open(const QString &path, QString *error);
    qint64 cachePlaylistId() const { return m_cachePlaylist; }

    // Réglages (servis depuis la mémoire, écrits en base)
    QString setting(const QString &key, const QString &def = {}) const;
    bool settingBool(const QString &key, bool def) const;
    void setSetting(const QString &key, const QString &value);
    void removeSetting(const QString &key);

    // Morceaux
    Track upsertTrack(const Track &t);  // identifié par ytId
    std::optional<Track> track(qint64 id) const;
    std::optional<Track> trackByYt(const QString &ytId) const;
    std::optional<Track> findLocalMatch(const QString &spotifyId, const QString &artist, const QString &title) const;
    void rememberMatch(const QString &artist, const QString &title, const QString &ytId);
    void setState(qint64 id, const QString &state, const QString &error = {});
    void setFile(qint64 id, const QString &path, const QString &codec, double bitrate, qint64 size, double duration);
    void enrichMetadata(qint64 id, const QString &title, const QString &artist, const QString &album);
    void clearFile(qint64 id);
    // Lecture commencée : date de dernière écoute et playlist « Cache local »
    // (le compteur, lui, n'avance qu'après la moitié du morceau : recordPlay).
    void markStarted(qint64 id);
    QList<Track> tracksInState(const QString &state) const;
    QList<Track> searchLocal(const QString &q, int limit) const;

    // Écoutes (comptées au-delà de 50 % du morceau réellement écouté)
    qint64 recordPlay(qint64 trackId, double listened);
    void updatePlay(qint64 playId, double listened);
    ListenStats listenStats(qint64 since) const;
    QList<TopTrack> topTracks(qint64 since, int limit) const;
    QList<TopArtist> topArtists(qint64 since, int limit, int *distinct = nullptr) const;

    // Paroles
    std::optional<StoredLyrics> lyrics(qint64 trackId) const;
    void saveLyrics(qint64 trackId, const StoredLyrics &l);
    // Morceaux en local sans paroles (ou introuvables depuis plus d'une semaine).
    QList<Track> tracksNeedingLyrics(int limit) const;

    // Cache
    QPair<qint64, int> cacheSize() const;
    QList<Track> evictionCandidates() const;
    bool isPinned(qint64 trackId) const;

    // Playlists
    QList<Playlist> playlists() const;
    std::optional<Playlist> playlist(qint64 id) const;
    qint64 createPlaylist(const QString &name, const QString &kind, const QString &sourceRef = {});
    std::optional<qint64> playlistBySource(const QString &kind, const QString &sourceRef) const;
    void renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    QList<Track> playlistTracks(qint64 id) const;
    void addToPlaylist(qint64 playlistId, qint64 trackId);
    // Playlists contenant déjà ce morceau (true) ou une autre version du même titre (false).
    QHash<qint64, bool> playlistsContaining(qint64 trackId, const QString &matchKey) const;
    void removeFromPlaylist(qint64 playlistId, qint64 trackId);
    void moveInPlaylist(qint64 playlistId, qint64 trackId, int newIndex);

    // Imports
    qint64 createJob(const QString &source, const QString &name);
    void updateJob(const ImportJob &job);
    QList<ImportJob> jobs() const;
    void clearFinishedJobs();

private:
    QList<Track> queryTracks(const QString &sql, const QVariantList &binds = {}) const;
    QSqlDatabase db() const;
    qint64 m_cachePlaylist = 0;
    // Copie en mémoire de la table settings : lue très souvent (à chaque morceau,
    // chaque téléchargement), elle n'est écrite qu'en cas de changement.
    QHash<QString, QString> m_settings;
};
