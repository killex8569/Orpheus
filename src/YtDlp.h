#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <functional>
#include <optional>

#include "Matcher.h"

#include "Track.h"

// Pilotage asynchrone de yt-dlp (recherche, playlists, flux, téléchargement).
// yt-dlp reste la seule solution fiable face aux protections de YouTube.
class YtDlp : public QObject {
    Q_OBJECT
public:
    struct DownloadResult {
        QString path;
        QString codec;
        double bitrate = 0;
        double duration = 0;
        QString title, artist, album;
        QString rawTitle, channel;  // titre brut de la vidéo et nom de la chaîne
    };

    using TracksCb = std::function<void(QList<Track>, QString error)>;

    explicit YtDlp(const QString &managedDir, QObject *parent = nullptr);

    QString binary() const { return m_bin; }
    bool hasDeno() const { return !m_deno.isEmpty(); }
    bool isManaged() const;
    void setCookiesBrowser(const QString &b) { m_cookiesBrowser = b; }
    // Recense les copies de yt-dlp installées (copie gérée, PATH, dossiers usuels).
    void redetect();
    // Interroge chaque copie et retient la plus récente : une version de quelques
    // semaines suffit à se faire refuser par YouTube (HTTP 403).
    void selectNewest(std::function<void(QString version)> cb);

    void version(std::function<void(QString)> cb);
    void search(const QString &query, int limit, TracksCb cb);
    // Meilleur équivalent YouTube d'un morceau (import Spotify).
    void findMatch(const QString &artist, const QString &title, double duration, std::function<void(std::optional<Track>, QString)> cb);
    void playlist(const QString &url, std::function<void(QString title, QList<Track>, QString error)> cb);
    void accountPlaylists(std::function<void(QList<RemotePlaylist>, QString error)> cb);
    void resolveStream(const QString &ytId, std::function<void(QString url, QString userAgent, QString error)> cb);
    // Renvoie le processus pour permettre l'annulation.
    QProcess *download(const QString &ytId, const QString &libraryDir, const QString &coversDir,
                       std::function<void(double)> progress, std::function<void(DownloadResult, QString error)> done);
    // Installe/met à jour une copie de yt-dlp gérée par Orpheus (toujours récente).
    void updateManaged(std::function<void(QString message, bool ok)> cb);

private:
    QStringList baseArgs() const;
    void runJson(const QStringList &args, int timeoutMs, std::function<void(QJsonDocument, QString)> cb);
    void searchRaw(const QString &query, int limit, std::function<void(QList<Track>, QList<Matcher::Candidate>, QString)> cb);

    QString m_managedDir;
    QString m_bin;
    QStringList m_candidates;
    QString m_deno;
    QString m_cookiesBrowser;
};

QString ytdlpError(const QString &stderrText);
