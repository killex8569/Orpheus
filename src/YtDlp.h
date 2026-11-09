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
    bool hasJsRuntime() const { return !m_jsRuntime.isEmpty(); }
    // « deno » ou « node » (vide : aucun moteur JavaScript trouvé).
    QString jsRuntimeName() const { return m_jsRuntime.section(':', 0, 0); }
    bool isManaged() const;
    void setCookiesBrowser(const QString &b)
    {
        m_cookiesBrowser = b;
        m_cookiesBroken = false;
    }
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
    // Valeur d'un cookie du navigateur (ex. « sp_dc » de spotify.com). yt-dlp sait lire et
    // déchiffrer les bases de cookies des navigateurs ; leur copie temporaire, privée,
    // est effacée aussitôt lue. Rien n'est envoyé sur le réseau.
    void browserCookie(const QString &browser, const QString &domain, const QString &name,
                       std::function<void(QString value, QString error)> cb);

signals:
    // Les cookies du navigateur choisi sont illisibles (navigateur ouvert, chiffrement
    // Windows…) : Orpheus continue sans eux jusqu'au prochain changement de réglage.
    void cookiesUnavailable(const QString &browser);

private:
    QStringList baseArgs() const;
    // Lance yt-dlp (`bin`) ; sous Android, par le Python embarqué (AndroidBridge).
    void startProcess(QProcess *p, const QString &bin, const QStringList &args) const;
    // Désactive les cookies si l'erreur vient de leur lecture ; true si c'est le cas.
    bool dropBrokenCookies(const QString &stderrText);
    void runJson(const QStringList &args, int timeoutMs, std::function<void(QJsonDocument, QString)> cb);
    void searchRaw(const QString &query, int limit, std::function<void(QList<Track>, QList<Matcher::Candidate>, QString)> cb);

    QString m_managedDir;
    QString m_bin;
    QStringList m_candidates;
    QString m_jsRuntime;  // « deno:/chemin » ou « node:/chemin »
    QString m_cookiesBrowser;
    bool m_cookiesBroken = false;
#ifdef Q_OS_ANDROID
    bool m_androidReady = false;  // Python et yt-dlp décompressés (youtubedl-android)
#endif
};

QString ytdlpError(const QString &stderrText);
// Valeur d'un cookie dans un fichier au format Netscape (écrit par yt-dlp) ; "" si absent.
QString netscapeCookie(const QString &text, const QString &domain, const QString &name);
