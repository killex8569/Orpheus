#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <functional>

#include "Database.h"
#include "Track.h"

class QTcpServer;

// Spotify : connexion OAuth PKCE (sans secret), lecture des playlists, recherche.
// Spotify ne permet pas de récupérer l'audio (DRM) : on importe les métadonnées,
// puis chaque morceau est retrouvé sur YouTube.
class Spotify : public QObject {
    Q_OBJECT
public:
    static constexpr const char *RedirectUri = "http://127.0.0.1:8765/callback";

    explicit Spotify(Database *db, QObject *parent = nullptr);

    bool isConnected() const;
    QString userName() const { return m_db->setting("spotify_user"); }
    void logout();
    // Ouvre le navigateur et attend la redirection sur un serveur local éphémère.
    void login(std::function<void(QString error)> cb);
    void playlists(std::function<void(QList<RemotePlaylist>, QString)> cb);
    void tracks(const QString &kind, const QString &id, std::function<void(QString name, QList<Track>, QString)> cb);
    void search(const QString &q, std::function<void(QList<Track>, QString)> cb);
    // Sans compte : page d'intégration publique (≈100 premiers titres).
    void publicTracks(const QString &kind, const QString &id, std::function<void(QString, QList<Track>, QString)> cb);
    // Sans compte : un morceau seul (sa page reste publique même dans une playlist privée).
    void publicTrack(const QString &id, std::function<void(std::optional<Track>, QString)> cb, int attempt = 0);

private:
    using JsonCb = std::function<void(QJsonObject, QString)>;
    void token(bool forceRefresh, std::function<void(QString token, QString err)> cb);
    void tokenRequest(const QList<QPair<QString, QString>> &form, std::function<void(QString, QString)> cb);
    void get(const QString &url, JsonCb cb, int attempt = 0, bool refreshed = false);
    void paged(const QString &url, std::function<void(QList<QJsonObject>, QString)> cb);
    void handleCallback(const QString &code, const QString &state, std::function<void(QString)> cb);

    Database *m_db;
    QNetworkAccessManager m_net;
    QString m_verifier, m_state;
    QTcpServer *m_server = nullptr;
    bool m_refreshing = false;
    QList<std::function<void(QString, QString)>> m_tokenWaiters;
};

// `open.spotify.com/playlist/ID`, `spotify:album:ID` → (kind, id).
QPair<QString, QString> parseSpotifyUrl(const QString &s);
// Identifiants des morceaux d'un texte collé (liens `open.spotify.com/track/…` ou `spotify:track:…`), sans doublon.
QStringList spotifyTrackIds(const QString &text);
