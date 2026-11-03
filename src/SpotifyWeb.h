#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>
#include <memory>

#include "Database.h"
#include "Track.h"

// Session du lecteur web de Spotify (open.spotify.com) : marche avec un compte GRATUIT,
// sans application développeur (que Spotify réserve depuis 2026 aux comptes Premium).
//
// Le cookie de connexion « sp_dc » du navigateur s'échange contre un jeton du lecteur web
// (code TOTP dérivé d'un secret publié dans le lecteur), puis les playlists sont lues par
// l'API GraphQL du lecteur (« pathfinder »). Sans cookie, un jeton anonyme lit les
// playlists publiques en entier (la page d'intégration s'arrête à ~100 titres).
//
// API non officielle : Spotify peut changer le secret ou les requêtes. Les empreintes des
// requêtes sont relues dans le lecteur en cas de refus, et la liste des secrets est
// complétée depuis un dépôt communautaire si le secret intégré ne suffit plus.
class SpotifyWeb : public QObject {
    Q_OBJECT
public:
    explicit SpotifyWeb(Database *db, QObject *parent = nullptr);

    bool isConnected() const { return !m_db->setting("spotify_sp_dc").isEmpty(); }
    // Origine du cookie : nom du navigateur, ou « manual » s'il a été collé.
    QString source() const { return m_db->setting("spotify_sp_dc_source"); }
    // Vérifie le cookie (Spotify doit renvoyer un jeton non anonyme), puis l'enregistre.
    void connectCookie(const QString &spDc, const QString &source, std::function<void(QString error)> cb);
    void logout();

    // Playlists de la bibliothèque (« Titres likés » en tête). Compte requis.
    void playlists(std::function<void(QList<RemotePlaylist>, QString error)> cb);
    // Titres likés. Compte requis.
    void likedTracks(std::function<void(QString name, QList<Track>, QString error)> cb);
    // Morceaux d'une playlist : privée si le compte connecté y a accès, sinon publique.
    void playlistTracks(const QString &id, std::function<void(QString name, QList<Track>, QString error)> cb);

private:
    struct Token {
        QString access, clientId;
        qint64 expiresMs = 0;
    };
    using TokenCb = std::function<void(Token, QString)>;
    using JsonCb = std::function<void(QJsonObject data, QString error)>;
    // Une page de réponse → morceaux ; raw : éléments bruts de la page (pour avancer).
    using PageParser = std::function<QList<Track>(const QJsonObject &data, int *total, int *raw, QString *name, QString *error)>;
    using TracksCb = std::function<void(QString name, QList<Track>, QString error)>;

    void token(bool user, bool force, TokenCb cb);
    void fetchToken(const QString &spDc, TokenCb cb);
    void requestToken(const QString &spDc, qint64 serverTime, int version, const QString &reason, bool remoteTried, TokenCb cb);
    void loadRemoteSecrets(std::function<void(bool newer)> cb);
    void query(const QString &op, const QJsonObject &vars, bool user, JsonCb cb, int attempt = 0, bool refreshed = false,
               bool rediscovered = false);
    void discoverHashes(std::function<void(QString error)> cb);
    void pagedTracks(const QString &op, const QJsonObject &vars, bool user, int pageSize, PageParser parse, TracksCb cb);
    void libraryPage(bool flatten, std::shared_ptr<QList<RemotePlaylist>> acc, int offset,
                     std::function<void(QList<RemotePlaylist>, QString)> cb);
    QHash<int, QList<int>> secrets() const;
    QString hash(const QString &op) const;

    Database *m_db;
    QNetworkAccessManager m_net;
    Token m_user, m_anon;
    // Demandes de jeton en attente (une seule requête à la fois par type de jeton).
    QList<TokenCb> m_waiters[2];
    bool m_remoteSecretsLoaded = false;
};

// Date d'un en-tête HTTP (« Tue, 03 Nov 2026 10:20:00 GMT ») en secondes ; 0 si illisible.
qint64 httpDateSecs(const QByteArray &date);

// ---------- Fonctions pures (testées) ----------

// Code TOTP (6 chiffres, 30 s, HMAC-SHA1) du lecteur web pour un secret « chiffré ».
QString spotifyTotp(const QList<int> &cipher, qint64 unixTime);
// Empreintes des requêtes GraphQL citées dans le code du lecteur web (nom → sha256).
QHash<QString, QString> pathfinderHashes(const QString &js);
// Réponses GraphQL du lecteur web → morceaux / playlists. total : nombre annoncé (-1 inconnu).
QList<Track> pathfinderPlaylistTracks(const QJsonObject &playlistV2, int *total, QString *name);
QList<Track> pathfinderLikedTracks(const QJsonObject &tracksPage, int *total);
QList<RemotePlaylist> pathfinderLibrary(const QJsonObject &libraryPage, int *total);
