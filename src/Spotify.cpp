#include "Spotify.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <memory>

static const char *kApi = "https://api.spotify.com/v1";
static const char *kScopes = "playlist-read-private playlist-read-collaborative user-library-read";

namespace {

QString randomToken(int bytes)
{
    QByteArray buf(bytes, 0);
    QRandomGenerator::system()->generate(buf.begin(), buf.end());
    return QString::fromLatin1(buf.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::optional<Track> parseTrack(const QJsonObject &t, const QJsonObject &albumFallback = {})
{
    if (t.isEmpty() || t["is_local"].toBool() || (t.contains("type") && t["type"].toString() != "track"))
        return std::nullopt;
    const QString name = t["name"].toString();
    if (name.isEmpty())
        return std::nullopt;
    QStringList artists;
    for (const QJsonValue &a : t["artists"].toArray())
        artists << a.toObject()["name"].toString();
    const QJsonObject album = t["album"].isObject() ? t["album"].toObject() : albumFallback;
    Track tr;
    tr.title = name;
    tr.artist = artists.join(", ");
    tr.album = album["name"].toString();
    tr.duration = t["duration_ms"].toDouble() / 1000.0;
    tr.thumbnail = album["images"].toArray().at(0).toObject()["url"].toString();
    tr.spotifyId = t["id"].toString();
    return tr;
}

const char *kBrowserUa = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130 Safari/537.36";

// Données de la page d'intégration (`__NEXT_DATA__` → props.pageProps).
QJsonObject embedPageProps(const QString &html)
{
    static const QRegularExpression re(QStringLiteral("<script id=\"__NEXT_DATA__\"[^>]*>(.*?)</script>"),
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    if (!m.hasMatch())
        return {};
    return QJsonDocument::fromJson(m.captured(1).toUtf8()).object()["props"].toObject()["pageProps"].toObject();
}

}  // namespace

QStringList spotifyTrackIds(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(?:spotify:|open\\.spotify\\.com/(?:intl-[a-z]+/)?)track[:/]([A-Za-z0-9]{22})"));
    QStringList ids;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QString id = it.next().captured(1);
        if (!ids.contains(id))
            ids << id;
    }
    return ids;
}

QPair<QString, QString> parseSpotifyUrl(const QString &input)
{
    static const QRegularExpression re(QStringLiteral("(?:spotify:|open\\.spotify\\.com/(?:intl-[a-z]+/)?)(playlist|album)[:/]([A-Za-z0-9]+)"));
    const auto m = re.match(input.trimmed());
    return m.hasMatch() ? QPair<QString, QString>(m.captured(1), m.captured(2)) : QPair<QString, QString>();
}

Spotify::Spotify(Database *db, QObject *parent) : QObject(parent), m_db(db)
{
    m_net.setTransferTimeout(30000);
}

bool Spotify::isConnected() const
{
    return !m_db->setting("spotify_refresh").isEmpty();
}

void Spotify::logout()
{
    for (const char *k : {"spotify_access", "spotify_refresh", "spotify_expires", "spotify_user"})
        m_db->removeSetting(k);
}

void Spotify::login(std::function<void(QString)> cb)
{
    const QString clientId = m_db->setting("spotify_client_id").trimmed();
    if (clientId.isEmpty()) {
        cb(QStringLiteral("renseignez d'abord le Client ID Spotify"));
        return;
    }
    if (m_server) {
        m_server->close();  // libère le port 8765 tout de suite (deleteLater est différé)
        m_server->deleteLater();
    }
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 8765)) {
        cb(QStringLiteral("impossible d'écouter sur le port 8765 (déjà utilisé ?)"));
        return;
    }
    m_verifier = randomToken(64);
    m_state = randomToken(16);
    const QByteArray challenge = QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url("https://accounts.spotify.com/authorize");
    QUrlQuery q;
    q.addQueryItem("response_type", "code");
    q.addQueryItem("client_id", clientId);
    q.addQueryItem("scope", kScopes);
    q.addQueryItem("redirect_uri", RedirectUri);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("state", m_state);
    url.setQuery(q);

    QPointer<QTcpServer> server = m_server;
    auto done = std::make_shared<bool>(false);
    QTimer::singleShot(5 * 60 * 1000, this, [server, cb, done] {
        if (*done || !server)
            return;
        *done = true;
        server->close();
        server->deleteLater();
        cb(QStringLiteral("connexion Spotify abandonnée (délai dépassé)"));
    });
    connect(m_server, &QTcpServer::newConnection, this, [this, server, cb, done] {
        while (server && server->hasPendingConnections()) {
            QTcpSocket *sock = server->nextPendingConnection();
            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            connect(sock, &QTcpSocket::readyRead, this, [this, sock, server, cb, done] {
                const QString req = QString::fromUtf8(sock->readAll());
                const QString path = req.section(' ', 1, 1);
                if (!path.startsWith("/callback?")) {
                    sock->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                    sock->disconnectFromHost();
                    return;
                }
                const QUrlQuery q(path.mid(10));
                auto reply = [sock](const QString &msg) {
                    const QByteArray body =
                        QStringLiteral("<!doctype html><meta charset=utf-8><title>Orpheus</title>"
                                       "<body style=\"font-family:sans-serif;background:#111;color:#eee;display:grid;"
                                       "place-items:center;height:100vh\"><h2>%1</h2>")
                            .arg(msg.toHtmlEscaped())
                            .toUtf8();
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    sock->disconnectFromHost();
                };
                auto finish = [server, cb, done, reply](const QString &err) {
                    reply(err.isEmpty() ? QStringLiteral("Compte Spotify connecté. Vous pouvez revenir dans Orpheus.")
                                        : QStringLiteral("Échec de la connexion : ") + err);
                    if (*done)
                        return;
                    *done = true;
                    if (server) {
                        server->close();
                        server->deleteLater();
                    }
                    cb(err);
                };
                if (q.hasQueryItem("error"))
                    finish(QStringLiteral("connexion refusée (%1)").arg(q.queryItemValue("error")));
                else
                    handleCallback(q.queryItemValue("code"), q.queryItemValue("state"), finish);
            });
        }
    });
    QDesktopServices::openUrl(url);
}

void Spotify::handleCallback(const QString &code, const QString &state, std::function<void(QString)> cb)
{
    if (code.isEmpty() || state != m_state) {
        cb(QStringLiteral("réponse Spotify invalide"));
        return;
    }
    tokenRequest({{"grant_type", "authorization_code"},
                  {"code", code},
                  {"redirect_uri", RedirectUri},
                  {"client_id", m_db->setting("spotify_client_id").trimmed()},
                  {"code_verifier", m_verifier}},
                 [this, cb](QString, QString err) {
                     if (!err.isEmpty()) {
                         cb(err);
                         return;
                     }
                     get(QString(kApi) + "/me", [this, cb](QJsonObject me, QString) {
                         const QString name = me["display_name"].toString(me["id"].toString());
                         if (!name.isEmpty())
                             m_db->setSetting("spotify_user", name);
                         cb({});
                     });
                 });
}

void Spotify::tokenRequest(const QList<QPair<QString, QString>> &form, std::function<void(QString, QString)> cb)
{
    QNetworkRequest req(QUrl("https://accounts.spotify.com/api/token"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QUrlQuery body;
    for (const auto &kv : form)
        body.addQueryItem(kv.first, QString::fromLatin1(QUrl::toPercentEncoding(kv.second)));
    QNetworkReply *r = m_net.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(r, &QNetworkReply::finished, this, [this, r, cb] {
        r->deleteLater();
        const QJsonObject v = QJsonDocument::fromJson(r->readAll()).object();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) {
            if (v["error"].toString() == "invalid_grant") {
                logout();
                cb({}, QStringLiteral("session Spotify expirée, reconnectez-vous"));
            } else {
                cb({}, QStringLiteral("Spotify a refusé l'authentification : %1")
                           .arg(v["error_description"].toString(r->errorString())));
            }
            return;
        }
        const QString access = v["access_token"].toString();
        m_db->setSetting("spotify_access", access);
        if (v.contains("refresh_token"))
            m_db->setSetting("spotify_refresh", v["refresh_token"].toString());
        m_db->setSetting("spotify_expires", QString::number(nowSecs() + v["expires_in"].toInt(3600) - 60));
        cb(access, {});
    });
}

void Spotify::token(bool forceRefresh, std::function<void(QString, QString)> cb)
{
    if (!forceRefresh && m_db->setting("spotify_expires").toLongLong() > nowSecs()) {
        const QString t = m_db->setting("spotify_access");
        if (!t.isEmpty()) {
            cb(t, {});
            return;
        }
    }
    const QString refresh = m_db->setting("spotify_refresh");
    if (refresh.isEmpty()) {
        cb({}, QStringLiteral("compte Spotify non connecté (Paramètres)"));
        return;
    }
    // Un seul rafraîchissement à la fois ; les autres demandes attendent.
    m_tokenWaiters.append(cb);
    if (m_refreshing)
        return;
    m_refreshing = true;
    tokenRequest({{"grant_type", "refresh_token"}, {"refresh_token", refresh}, {"client_id", m_db->setting("spotify_client_id").trimmed()}},
                 [this](QString t, QString err) {
                     m_refreshing = false;
                     const auto waiters = std::exchange(m_tokenWaiters, {});
                     for (const auto &w : waiters)
                         w(t, err);
                 });
}

void Spotify::get(const QString &url, JsonCb cb, int attempt, bool refreshed)
{
    token(false, [=, this](QString tok, QString err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Authorization", "Bearer " + tok.toLatin1());
        QNetworkReply *r = m_net.get(req);
        connect(r, &QNetworkReply::finished, this, [=, this] {
            r->deleteLater();
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = r->readAll();
            if (status >= 200 && status < 300) {
                cb(QJsonDocument::fromJson(body).object(), {});
            } else if (status == 401 && !refreshed) {
                token(true, [=, this](QString, QString e) {
                    if (!e.isEmpty())
                        cb({}, e);
                    else
                        get(url, cb, attempt, true);
                });
            } else if ((status == 429 || status >= 500 || status == 0) && attempt < 4) {
                int wait = r->rawHeader("Retry-After").toInt();
                if (wait <= 0)
                    wait = 1 << attempt;
                QTimer::singleShot(std::min(wait, 30) * 1000, this, [=, this] { get(url, cb, attempt + 1, refreshed); });
            } else {
                const QString msg = QJsonDocument::fromJson(body).object()["error"].toObject()["message"].toString(r->errorString());
                cb({}, QStringLiteral("Spotify (%1) : %2").arg(status).arg(msg));
            }
        });
    });
}

void Spotify::paged(const QString &url, std::function<void(QList<QJsonObject>, QString)> cb)
{
    auto items = std::make_shared<QList<QJsonObject>>();
    auto step = std::make_shared<std::function<void(QString)>>();
    std::weak_ptr<std::function<void(QString)>> weak = step;  // pas de cycle (fuite)
    *step = [this, items, cb, weak](QString next) {
        get(next, [items, cb, step = weak.lock()](QJsonObject v, QString err) {
            if (!err.isEmpty()) {
                cb({}, err);
                return;
            }
            const QJsonObject page = v["items"].isArray() ? v : v["tracks"].toObject();
            for (const QJsonValue &i : page["items"].toArray())
                items->append(i.toObject());
            const QString nextUrl = page["next"].toString();
            if (!nextUrl.isEmpty() && items->size() < 20000)
                (*step)(nextUrl);
            else
                cb(*items, {});
        });
    };
    (*step)(url);
}

void Spotify::playlists(std::function<void(QList<RemotePlaylist>, QString)> cb)
{
    paged(QString(kApi) + "/me/playlists?limit=50", [cb](QList<QJsonObject> items, QString err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        QList<RemotePlaylist> out{{"liked", QStringLiteral("Titres likés"), "spotify:liked", -1}};
        for (const QJsonObject &p : items) {
            const QString id = p["id"].toString();
            if (id.isEmpty())
                continue;
            const QJsonObject counts = p["items"].isObject() ? p["items"].toObject() : p["tracks"].toObject();
            out.append({id, p["name"].toString(id), "https://open.spotify.com/playlist/" + id, counts["total"].toInt(-1)});
        }
        cb(out, {});
    });
}

void Spotify::tracks(const QString &kind, const QString &id, std::function<void(QString, QList<Track>, QString)> cb)
{
    if (id == "liked") {
        paged(QString(kApi) + "/me/tracks?limit=50", [cb](QList<QJsonObject> items, QString err) {
            QList<Track> out;
            for (const QJsonObject &i : items)
                if (auto t = parseTrack(i["track"].toObject()))
                    out.append(*t);
            cb(QStringLiteral("Titres likés (Spotify)"), out, err);
        });
        return;
    }
    if (kind == "album") {
        get(QString(kApi) + "/albums/" + id, [this, id, cb](QJsonObject album, QString err) {
            if (!err.isEmpty()) {
                cb({}, {}, err);
                return;
            }
            paged(QString(kApi) + "/albums/" + id + "/tracks?limit=50", [album, cb](QList<QJsonObject> items, QString err) {
                QList<Track> out;
                for (const QJsonObject &t : items)
                    if (auto tr = parseTrack(t, album))
                        out.append(*tr);
                cb(album["name"].toString("Album"), out, err);
            });
        });
        return;
    }
    get(QString(kApi) + "/playlists/" + id + "?fields=name", [this, id, cb](QJsonObject meta, QString err) {
        if (!err.isEmpty()) {
            cb({}, {}, err);
            return;
        }
        const QString name = meta["name"].toString("Playlist Spotify");
        auto parse = [name, cb](QList<QJsonObject> items, QString err) {
            QList<Track> out;
            for (const QJsonObject &i : items) {
                const QJsonObject t = i["item"].isObject() ? i["item"].toObject() : i["track"].toObject();
                if (auto tr = parseTrack(t))
                    out.append(*tr);
            }
            cb(name, out, err);
        };
        // Nouveau point d'accès `/items`, l'ancien `/tracks` en repli.
        paged(QString(kApi) + "/playlists/" + id + "/items?limit=50", [this, id, parse](QList<QJsonObject> items, QString err) {
            if (err.isEmpty())
                parse(items, {});
            else
                paged(QString(kApi) + "/playlists/" + id + "/tracks?limit=50", parse);
        });
    });
}

void Spotify::search(const QString &q, std::function<void(QList<Track>, QString)> cb)
{
    QUrl url(QString(kApi) + "/search");
    QUrlQuery query;
    query.addQueryItem("q", q);
    query.addQueryItem("type", "track");
    query.addQueryItem("limit", "10");
    url.setQuery(query);
    get(url.toString(QUrl::FullyEncoded), [cb](QJsonObject v, QString err) {
        QList<Track> out;
        for (const QJsonValue &t : v["tracks"].toObject()["items"].toArray())
            if (auto tr = parseTrack(t.toObject()))
                out.append(*tr);
        cb(out, err);
    });
}

void Spotify::publicTracks(const QString &kind, const QString &id, std::function<void(QString, QList<Track>, QString)> cb)
{
    QNetworkRequest req(QUrl(QStringLiteral("https://open.spotify.com/embed/%1/%2").arg(kind, id)));
    req.setHeader(QNetworkRequest::UserAgentHeader, kBrowserUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [r, cb, kind] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            cb({}, {}, QStringLiteral("page Spotify inaccessible : %1").arg(r->errorString()));
            return;
        }
        const QJsonObject props = embedPageProps(QString::fromUtf8(r->readAll()));
        if (props.isEmpty()) {
            cb({}, {}, QStringLiteral("page Spotify inattendue"));
            return;
        }
        if (props["status"].toInt() == 404) {
            cb({}, {},
               kind == "playlist"
                   ? QStringLiteral("playlist privée ou introuvable : connectez votre compte Spotify, ou utilisez "
                                    "« Playlist privée : coller les morceaux » ci-dessous")
                   : QStringLiteral("introuvable sur Spotify"));
            return;
        }
        const QJsonObject entity = props["state"].toObject()["data"].toObject()["entity"].toObject();
        const QString name = entity["name"].toString(entity["title"].toString("Playlist Spotify"));
        const QString cover = entity["coverArt"].toObject()["sources"].toArray().at(0).toObject()["url"].toString();
        QList<Track> out;
        for (const QJsonValue &v : entity["trackList"].toArray()) {
            const QJsonObject t = v.toObject();
            Track tr;
            tr.title = t["title"].toString();
            tr.artist = t["subtitle"].toString().replace(QChar(0xa0), ' ');
            tr.duration = t["duration"].toDouble() / 1000.0;
            tr.spotifyId = t["uri"].toString().section(':', -1);
            tr.thumbnail = cover;
            if (!tr.title.isEmpty())
                out.append(tr);
        }
        cb(name, out, out.isEmpty() ? QStringLiteral("aucun morceau public trouvé") : QString());
    });
}

void Spotify::publicTrack(const QString &id, std::function<void(std::optional<Track>, QString)> cb, int attempt)
{
    QNetworkRequest req(QUrl(QStringLiteral("https://open.spotify.com/embed/track/") + id));
    req.setHeader(QNetworkRequest::UserAgentHeader, kBrowserUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, cb, id, attempt] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError) {
            // Trop de demandes d'un coup ou coupure réseau : quelques nouvelles tentatives espacées.
            if ((status == 429 || status >= 500 || status == 0) && attempt < 3) {
                QTimer::singleShot(2000 * (attempt + 1), this, [=, this] { publicTrack(id, cb, attempt + 1); });
                return;
            }
            cb(std::nullopt, QStringLiteral("page Spotify inaccessible : %1").arg(r->errorString()));
            return;
        }
        const QJsonObject e = embedPageProps(QString::fromUtf8(r->readAll()))["state"].toObject()["data"]
                                  .toObject()["entity"].toObject();
        Track tr;
        tr.title = e["name"].toString(e["title"].toString());
        QStringList artists;
        for (const QJsonValue &a : e["artists"].toArray())
            artists << a.toObject()["name"].toString();
        tr.artist = artists.join(", ");
        tr.duration = e["duration"].toDouble() / 1000.0;
        tr.spotifyId = id;
        // Pochette ~300 px (la liste contient plusieurs tailles).
        for (const QJsonValue &img : e["visualIdentity"].toObject()["image"].toArray())
            if (tr.thumbnail.isEmpty() || img.toObject()["maxWidth"].toInt() == 300)
                tr.thumbnail = img.toObject()["url"].toString();
        if (tr.title.isEmpty())
            cb(std::nullopt, QStringLiteral("morceau Spotify introuvable"));
        else
            cb(tr, {});
    });
}
