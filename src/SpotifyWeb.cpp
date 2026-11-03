#include "SpotifyWeb.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>

namespace {

const char *kUa = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";
const char *kWebPlayer = "https://open.spotify.com/";
const char *kTokenUrl = "https://open.spotify.com/api/token";
const char *kQueryUrl = "https://api-partner.spotify.com/pathfinder/v2/query";
// Liste communautaire des secrets TOTP du lecteur web, complétée à chaque rotation.
const char *kSecretsUrl = "https://raw.githubusercontent.com/xyloflake/spot-secrets-go/main/secrets/secretDict.json";

// Secret v61, utilisé par le lecteur web depuis janvier 2026 (vérifié en novembre 2026).
constexpr int kSecretVersion = 61;
const QList<int> &builtinSecret()
{
    static const QList<int> s{44, 55, 47, 42, 70, 40, 34, 114, 76, 74, 50, 111, 120, 97,
                              75, 76, 94, 102, 43, 69, 49, 120, 118, 80, 64, 78};
    return s;
}

// Empreintes des requêtes relevées en novembre 2026 ; relues dans le lecteur si refusées.
const QHash<QString, QString> &builtinHashes()
{
    static const QHash<QString, QString> h{
        {"fetchPlaylist", "21ea11a6ac75a5f3e01fd365d29c18e0a4a8b7dc09f89bbb5d73910190c6acaf"},
        {"libraryV3", "390c78e5b951029bad359785e69b07b536a509c581cbcd0aded5e5067f187455"},
        {"fetchLibraryTracks", "087278b20b743578a6262c2b0b4bcd20d879c503cc359a2285baf083ef944240"},
    };
    return h;
}

const QString kLikedUri = QStringLiteral("spotify:collection:tracks");

// Réserve de cookies toujours vide. Avec la réserve par défaut, Qt garde les cookies posés
// par open.spotify.com (requête d'heure serveur) et les envoie À LA PLACE de l'en-tête
// « Cookie: sp_dc=… » ajouté à la main : Spotify ne voit alors qu'un visiteur anonyme.
class NoCookieJar : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;
    QList<QNetworkCookie> cookiesForUrl(const QUrl &) const override { return {}; }
    bool setCookiesFromUrl(const QList<QNetworkCookie> &, const QUrl &) override { return false; }
};

std::optional<Track> trackFrom(const QJsonObject &d, QString uri)
{
    if (d["__typename"].toString() != QLatin1String("Track"))
        return std::nullopt;  // épisode, fichier local, morceau supprimé…
    if (uri.isEmpty())
        uri = d["uri"].toString();
    if (!uri.startsWith(QLatin1String("spotify:track:")))
        return std::nullopt;
    Track t;
    t.title = d["name"].toString();
    if (t.title.isEmpty())
        return std::nullopt;
    QStringList artists;
    for (const QJsonValue &a : d["artists"].toObject()["items"].toArray())
        if (const QString n = a.toObject()["profile"].toObject()["name"].toString(); !n.isEmpty())
            artists << n;
    t.artist = artists.join(", ");
    const QJsonObject album = d["albumOfTrack"].toObject();
    t.album = album["name"].toString();
    const QJsonObject dur = d["trackDuration"].isObject() ? d["trackDuration"].toObject() : d["duration"].toObject();
    t.duration = dur["totalMilliseconds"].toDouble() / 1000.0;
    // Pochette ~300 px (la liste contient plusieurs tailles).
    for (const QJsonValue &s : album["coverArt"].toObject()["sources"].toArray())
        if (t.thumbnail.isEmpty() || s.toObject()["width"].toInt() == 300)
            t.thumbnail = s.toObject()["url"].toString();
    t.spotifyId = uri.section(':', -1);
    return t;
}

QJsonObject parseJson(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object();
}

}  // namespace

// ---------- Fonctions pures ----------

QString spotifyTotp(const QList<int> &cipher, qint64 unixTime)
{
    // Secret : chaque octet XOR ((i % 33) + 9), nombres décimaux mis bout à bout.
    QByteArray key;
    for (int i = 0; i < cipher.size(); ++i)
        key += QByteArray::number(cipher[i] ^ ((i % 33) + 9));
    QByteArray counter(8, 0);
    qToBigEndian<quint64>(quint64(unixTime / 30), counter.data());
    const QByteArray h = QMessageAuthenticationCode::hash(counter, key, QCryptographicHash::Sha1);
    const int o = h.at(h.size() - 1) & 0x0f;
    const quint32 bin = (quint32(uchar(h[o]) & 0x7f) << 24) | (quint32(uchar(h[o + 1])) << 16)
                        | (quint32(uchar(h[o + 2])) << 8) | quint32(uchar(h[o + 3]));
    return QStringLiteral("%1").arg(bin % 1000000, 6, 10, QChar('0'));
}

qint64 httpDateSecs(const QByteArray &date)
{
    // Qt lit les décalages numériques (« +0000 ») mais pas « GMT », imposé par HTTP.
    QString s = QString::fromLatin1(date).trimmed();
    if (s.endsWith(QLatin1String(" GMT")))
        s = s.chopped(4) + QStringLiteral(" +0000");
    const QDateTime dt = QDateTime::fromString(s, Qt::RFC2822Date);
    return dt.isValid() ? dt.toSecsSinceEpoch() : 0;
}

QHash<QString, QString> pathfinderHashes(const QString &js)
{
    static const QRegularExpression re(
        QStringLiteral(R"(["']([A-Za-z0-9_]+)["']\s*,\s*["'](?:query|mutation)["']\s*,\s*["']([0-9a-f]{64})["'])"));
    QHash<QString, QString> out;
    for (auto it = re.globalMatch(js); it.hasNext();) {
        const auto m = it.next();
        out.insert(m.captured(1), m.captured(2));
    }
    return out;
}

QList<Track> pathfinderPlaylistTracks(const QJsonObject &playlistV2, int *total, QString *name)
{
    if (name)
        *name = playlistV2["name"].toString();
    const QJsonObject content = playlistV2["content"].toObject();
    if (total)
        *total = content["totalCount"].toInt(-1);
    QList<Track> out;
    for (const QJsonValue &v : content["items"].toArray()) {
        const QJsonObject d = v.toObject()["itemV2"].toObject()["data"].toObject();
        if (auto t = trackFrom(d, {}))
            out.append(*t);
    }
    return out;
}

QList<Track> pathfinderLikedTracks(const QJsonObject &tracksPage, int *total)
{
    if (total)
        *total = tracksPage["totalCount"].toInt(-1);
    QList<Track> out;
    for (const QJsonValue &v : tracksPage["items"].toArray()) {
        const QJsonObject wrapper = v.toObject()["track"].toObject();
        if (auto t = trackFrom(wrapper["data"].toObject(), wrapper["_uri"].toString()))
            out.append(*t);
    }
    return out;
}

QList<RemotePlaylist> pathfinderLibrary(const QJsonObject &libraryPage, int *total)
{
    if (total)
        *total = libraryPage["totalCount"].toInt(-1);
    QList<RemotePlaylist> out;
    for (const QJsonValue &v : libraryPage["items"].toArray()) {
        const QJsonObject item = v.toObject()["item"].toObject();
        const QString type = item["__typename"].toString();
        const QJsonObject d = item["data"].toObject();
        const QString uri = d["uri"].toString(item["_uri"].toString());
        if (type == QLatin1String("PlaylistResponseWrapper") && d["__typename"].toString() == QLatin1String("Playlist")) {
            const QString id = uri.section(':', -1);
            if (!uri.startsWith(QLatin1String("spotify:playlist:")) || id.isEmpty())
                continue;
            const QJsonObject content = d["content"].toObject();
            out.append({id, d["name"].toString(id), "https://open.spotify.com/playlist/" + id,
                        content.contains("totalCount") ? content["totalCount"].toInt(-1) : -1});
        } else if (type.contains(QLatin1String("PseudoPlaylist")) && uri == kLikedUri) {
            out.append({"liked", QStringLiteral("Titres likés"), "spotify:liked", d.contains("count") ? d["count"].toInt(-1) : -1});
        }
        // Dossiers, artistes, albums, podcasts : ignorés.
    }
    return out;
}

// ---------- Session ----------

SpotifyWeb::SpotifyWeb(Database *db, QObject *parent) : QObject(parent), m_db(db)
{
    m_net.setTransferTimeout(30000);
    m_net.setCookieJar(new NoCookieJar(&m_net));
}

void SpotifyWeb::logout()
{
    for (const char *k : {"spotify_sp_dc", "spotify_sp_dc_source"})
        m_db->removeSetting(k);
    m_user = {};
}

void SpotifyWeb::connectCookie(const QString &raw, const QString &source, std::function<void(QString)> cb)
{
    // Valeur seule, ou « sp_dc=… » copié depuis les outils du navigateur.
    QString v = raw.trimmed();
    if (v.startsWith(QLatin1String("sp_dc="), Qt::CaseInsensitive))
        v = v.mid(6);
    v = v.section(';', 0, 0).trimmed();
    if (v.startsWith('"') && v.endsWith('"') && v.size() > 1)
        v = v.mid(1, v.size() - 2);
    if (v.isEmpty()) {
        cb(QStringLiteral("cookie sp_dc vide"));
        return;
    }
    fetchToken(v, [this, v, source, cb](Token t, QString err) {
        if (!err.isEmpty()) {
            cb(err);
            return;
        }
        m_db->setSetting("spotify_sp_dc", v);
        m_db->setSetting("spotify_sp_dc_source", source);
        m_user = t;
        cb({});
    });
}

QHash<int, QList<int>> SpotifyWeb::secrets() const
{
    QHash<int, QList<int>> out{{kSecretVersion, builtinSecret()}};
    const QJsonObject cached = parseJson(m_db->setting("spotify_totp_secrets").toUtf8());
    for (auto it = cached.begin(); it != cached.end(); ++it) {
        bool ok = false;
        const int version = it.key().toInt(&ok);
        QList<int> bytes;
        for (const QJsonValue &b : it.value().toArray())
            bytes << b.toInt();
        if (ok && !bytes.isEmpty())
            out.insert(version, bytes);
    }
    return out;
}

void SpotifyWeb::loadRemoteSecrets(std::function<void(bool)> cb)
{
    if (m_remoteSecretsLoaded) {
        cb(false);
        return;
    }
    m_remoteSecretsLoaded = true;
    QNetworkRequest req{QUrl(kSecretsUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, cb] {
        r->deleteLater();
        const auto known = secrets().keys();
        const int before = known.isEmpty() ? 0 : *std::max_element(known.begin(), known.end());
        const QJsonObject dict = parseJson(r->readAll());
        // Format attendu : {"61": [44, 55, …], …} ; tout le reste est ignoré.
        QJsonObject valid = parseJson(m_db->setting("spotify_totp_secrets").toUtf8());
        int after = before;
        for (auto it = dict.begin(); it != dict.end(); ++it) {
            bool ok = false;
            const int version = it.key().toInt(&ok);
            const QJsonArray bytes = it.value().toArray();
            const bool allInts = !bytes.isEmpty() && std::all_of(bytes.begin(), bytes.end(), [](const QJsonValue &b) {
                return b.isDouble() && b.toDouble() >= 0 && b.toDouble() < 256 && b.toDouble() == b.toInt();
            });
            if (!ok || version <= 0 || !allInts)
                continue;
            valid.insert(it.key(), bytes);
            after = std::max(after, version);
        }
        if (!valid.isEmpty())
            m_db->setSetting("spotify_totp_secrets", QString::fromUtf8(QJsonDocument(valid).toJson(QJsonDocument::Compact)));
        cb(after > before);
    });
}

void SpotifyWeb::token(bool user, bool force, TokenCb cb)
{
    Token &t = user ? m_user : m_anon;
    if (!force && !t.access.isEmpty() && t.expiresMs > QDateTime::currentMSecsSinceEpoch() + 60000) {
        cb(t, {});
        return;
    }
    const QString spDc = user ? m_db->setting("spotify_sp_dc") : QString();
    if (user && spDc.isEmpty()) {
        cb({}, QStringLiteral("compte Spotify non connecté (Paramètres → Compte Spotify)"));
        return;
    }
    QList<TokenCb> &waiters = m_waiters[user ? 1 : 0];
    waiters.append(cb);
    if (waiters.size() > 1)
        return;  // une demande est déjà en cours
    fetchToken(spDc, [this, user](Token tok, QString err) {
        if (err.isEmpty())
            (user ? m_user : m_anon) = tok;
        for (const TokenCb &w : std::exchange(m_waiters[user ? 1 : 0], {}))
            w(tok, err);
    });
}

void SpotifyWeb::fetchToken(const QString &spDc, TokenCb cb)
{
    // Le code TOTP se calcule sur l'heure du serveur (en-tête Date) : une horloge
    // locale décalée de plus de 30 s le rendrait invalide.
    QNetworkRequest req{QUrl(kWebPlayer)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    QNetworkReply *r = m_net.head(req);
    connect(r, &QNetworkReply::finished, this, [this, r, spDc, cb] {
        r->deleteLater();
        qint64 now = httpDateSecs(r->rawHeader("Date"));
        if (now <= 0)
            now = QDateTime::currentSecsSinceEpoch();
        const auto versions = secrets().keys();
        const int newest = *std::max_element(versions.begin(), versions.end());
        requestToken(spDc, now, newest, spDc.isEmpty() ? QStringLiteral("init") : QStringLiteral("transport"), false, cb);
    });
}

void SpotifyWeb::requestToken(const QString &spDc, qint64 serverTime, int version, const QString &reason, bool remoteTried,
                              TokenCb cb)
{
    const QString code = spotifyTotp(secrets().value(version), serverTime);
    QUrl url(kTokenUrl);
    QUrlQuery q;
    q.addQueryItem("reason", reason);
    q.addQueryItem("productType", "web-player");
    q.addQueryItem("totp", code);
    q.addQueryItem("totpServer", code);
    q.addQueryItem("totpVer", QString::number(version));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Referer", kWebPlayer);
    req.setRawHeader("App-Platform", "WebPlayer");
    if (!spDc.isEmpty())
        req.setRawHeader("Cookie", "sp_dc=" + spDc.toLatin1());
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [=, this] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject v = parseJson(r->readAll());
        const QString access = v["accessToken"].toString();
        if (status == 200 && !access.isEmpty()) {
            if (!spDc.isEmpty() && v["isAnonymous"].toBool()) {
                // Le lecteur web essaie « transport », puis « init ».
                if (reason == QLatin1String("transport"))
                    requestToken(spDc, serverTime, version, QStringLiteral("init"), remoteTried, cb);
                else
                    cb({}, QStringLiteral("cookie Spotify expiré ou invalide : reconnectez-vous à open.spotify.com dans "
                                          "votre navigateur, puis de nouveau dans Orpheus (Paramètres)"));
                return;
            }
            Token t;
            t.access = access;
            t.clientId = v["clientId"].toString();
            t.expiresMs = qint64(v["accessTokenExpirationTimestampMs"].toDouble());
            if (t.expiresMs <= 0)
                t.expiresMs = QDateTime::currentMSecsSinceEpoch() + 30 * 60 * 1000;
            cb(t, {});
            return;
        }
        if (status == 0) {
            cb({}, QStringLiteral("Spotify injoignable : %1").arg(r->errorString()));
            return;
        }
        if (!spDc.isEmpty() && reason == QLatin1String("transport")) {
            requestToken(spDc, serverTime, version, QStringLiteral("init"), remoteTried, cb);
            return;
        }
        // Secret refusé : Spotify l'a peut-être changé ; la liste communautaire est consultée.
        if (!remoteTried) {
            loadRemoteSecrets([=, this](bool newer) {
                if (!newer) {
                    cb({}, QStringLiteral("Spotify a refusé la connexion du lecteur web (HTTP %1). Son fonctionnement a "
                                          "peut-être changé : mettez Orpheus à jour.").arg(status));
                    return;
                }
                const auto versions = secrets().keys();
                const int newest = *std::max_element(versions.begin(), versions.end());
                qInfo() << "Spotify : nouveau secret du lecteur web, version" << newest;
                requestToken(spDc, serverTime, newest, spDc.isEmpty() ? QStringLiteral("init") : QStringLiteral("transport"),
                             true, cb);
            });
            return;
        }
        cb({}, QStringLiteral("Spotify a refusé la connexion du lecteur web (HTTP %1). Son fonctionnement a peut-être "
                              "changé : mettez Orpheus à jour.").arg(status));
    });
}

// ---------- Requêtes GraphQL ----------

QString SpotifyWeb::hash(const QString &op) const
{
    const QString cached = parseJson(m_db->setting("spotify_gql_hashes").toUtf8())[op].toString();
    return cached.isEmpty() ? builtinHashes().value(op) : cached;
}

void SpotifyWeb::discoverHashes(std::function<void(QString)> cb)
{
    const QString fail = QStringLiteral("requêtes du lecteur web Spotify introuvables : mettez Orpheus à jour");
    QNetworkRequest req{QUrl(kWebPlayer)};
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, cb, fail] {
        r->deleteLater();
        static const QRegularExpression scriptRe(
            QStringLiteral(R"(<script[^>]+src=["']([^"']*/web-player/web-player\.[^"'/?]+\.js)["'])"));
        const auto m = scriptRe.match(QString::fromUtf8(r->readAll()));
        if (!m.hasMatch()) {
            cb(fail);
            return;
        }
        QNetworkRequest jsReq{QUrl(kWebPlayer).resolved(QUrl(m.captured(1)))};
        jsReq.setHeader(QNetworkRequest::UserAgentHeader, kUa);
        QNetworkReply *js = m_net.get(jsReq);
        connect(js, &QNetworkReply::finished, this, [this, js, cb, fail] {
            js->deleteLater();
            const QHash<QString, QString> found = pathfinderHashes(QString::fromUtf8(js->readAll()));
            QJsonObject keep;
            for (auto it = builtinHashes().cbegin(); it != builtinHashes().cend(); ++it)
                if (found.contains(it.key()))
                    keep.insert(it.key(), found.value(it.key()));
            if (keep.isEmpty()) {
                cb(fail);
                return;
            }
            m_db->setSetting("spotify_gql_hashes", QString::fromUtf8(QJsonDocument(keep).toJson(QJsonDocument::Compact)));
            cb({});
        });
    });
}

void SpotifyWeb::query(const QString &op, const QJsonObject &vars, bool user, JsonCb cb, int attempt, bool refreshed,
                       bool rediscovered)
{
    token(user, false, [=, this](Token tok, QString err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        const QJsonObject body{
            {"variables", vars},
            {"operationName", op},
            {"extensions", QJsonObject{{"persistedQuery", QJsonObject{{"version", 1}, {"sha256Hash", hash(op)}}}}},
        };
        QNetworkRequest req{QUrl(kQueryUrl)};
        req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json;charset=UTF-8");
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("App-Platform", "WebPlayer");
        req.setRawHeader("Authorization", "Bearer " + tok.access.toLatin1());
        if (!tok.clientId.isEmpty())
            req.setRawHeader("Client-Id", tok.clientId.toLatin1());
        QNetworkReply *r = m_net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(r, &QNetworkReply::finished, this, [=, this] {
            r->deleteLater();
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject v = parseJson(r->readAll());
            if (status == 401 && !refreshed) {
                token(user, true, [=, this](Token, QString e) {
                    if (!e.isEmpty())
                        cb({}, e);
                    else
                        query(op, vars, user, cb, attempt, true, rediscovered);
                });
                return;
            }
            if ((status == 429 || status >= 500 || status == 0) && attempt < 4) {
                int wait = r->rawHeader("Retry-After").toInt();
                if (wait <= 0)
                    wait = 1 << attempt;
                QTimer::singleShot(std::min(wait, 30) * 1000, this,
                                   [=, this] { query(op, vars, user, cb, attempt + 1, refreshed, rediscovered); });
                return;
            }
            QStringList messages;
            for (const QJsonValue &e : v["errors"].toArray())
                messages << e.toObject()["message"].toString();
            const QString msg = messages.join(" · ");
            // Empreinte périmée (Spotify répond 412) : relue dans le code actuel du lecteur, puis nouvel essai.
            if (!rediscovered
                && (status == 412 || status == 400 || status == 404 || msg.contains(QLatin1String("ersisted")))) {
                discoverHashes([=, this](QString e) {
                    if (!e.isEmpty())
                        cb({}, e);
                    else
                        query(op, vars, user, cb, attempt, refreshed, true);
                });
                return;
            }
            const QJsonObject data = v["data"].toObject();
            // Erreur sur un élément isolé (données partielles) : sans gravité. Racine absente : échec.
            const bool rootMissing =
                data.isEmpty() || std::all_of(data.begin(), data.end(), [](const QJsonValue &x) { return x.isNull(); });
            if (status != 200 || rootMissing) {
                if (msg.contains(QLatin1String("not authorized"), Qt::CaseInsensitive))
                    cb({}, user ? QStringLiteral("session Spotify expirée : reconnectez-vous (Paramètres → Compte Spotify)")
                                : QStringLiteral("compte Spotify requis (Paramètres → Compte Spotify)"));
                else
                    cb({}, QStringLiteral("lecteur web Spotify (%1) : %2")
                               .arg(status)
                               .arg(msg.isEmpty() ? r->errorString() : msg.left(200)));
                return;
            }
            cb(data, {});
        });
    });
}

void SpotifyWeb::pagedTracks(const QString &op, const QJsonObject &vars, bool user, int pageSize, PageParser parse, TracksCb cb)
{
    struct State {
        QList<Track> tracks;
        QString name;
        int offset = 0;
    };
    auto st = std::make_shared<State>();
    auto step = std::make_shared<std::function<void()>>();
    *step = [=, this] {
        QJsonObject v = vars;
        v["offset"] = st->offset;
        v["limit"] = pageSize;
        query(op, v, user, [=](QJsonObject data, QString err) {
            if (!err.isEmpty()) {
                cb(st->name, {}, err);
                return;
            }
            int total = -1, raw = 0;
            QString name, perr;
            const QList<Track> page = parse(data, &total, &raw, &name, &perr);
            if (!perr.isEmpty()) {
                cb(st->name, {}, perr);
                return;
            }
            if (st->name.isEmpty())
                st->name = name;
            st->tracks += page;
            st->offset += raw;
            if (raw > 0 && (total < 0 || st->offset < total) && st->offset < 20000)
                (*step)();
            else
                cb(st->name, st->tracks, {});
        });
    };
    (*step)();
}

void SpotifyWeb::playlistTracks(const QString &id, TracksCb cb)
{
    PageParser parse = [id](const QJsonObject &data, int *total, int *raw, QString *name, QString *error) {
        const QJsonObject p = data["playlistV2"].toObject();
        if (p["__typename"].toString() != QLatin1String("Playlist")) {
            *error = QStringLiteral("playlist introuvable ou privée (connectez le compte Spotify qui y a accès)");
            return QList<Track>();
        }
        *raw = int(p["content"].toObject()["items"].toArray().size());
        return pathfinderPlaylistTracks(p, total, name);
    };
    pagedTracks("fetchPlaylist", {{"uri", "spotify:playlist:" + id}, {"enableWatchFeedEntrypoint", false}}, isConnected(), 500,
                parse, [cb](QString name, QList<Track> tracks, QString err) {
                    cb(name.isEmpty() ? QStringLiteral("Playlist Spotify") : name, tracks, err);
                });
}

void SpotifyWeb::likedTracks(TracksCb cb)
{
    if (!isConnected()) {
        cb({}, {}, QStringLiteral("connectez votre compte Spotify (Paramètres) pour importer les titres likés"));
        return;
    }
    PageParser parse = [](const QJsonObject &data, int *total, int *raw, QString *, QString *error) {
        const QJsonObject page = data["me"].toObject()["library"].toObject()["tracks"].toObject();
        if (page.isEmpty()) {
            *error = QStringLiteral("titres likés illisibles (réponse Spotify inattendue)");
            return QList<Track>();
        }
        *raw = int(page["items"].toArray().size());
        return pathfinderLikedTracks(page, total);
    };
    // Pages de 50, comme le lecteur web.
    pagedTracks("fetchLibraryTracks", {}, true, 50, parse, [cb](QString, QList<Track> tracks, QString err) {
        cb(QStringLiteral("Titres likés (Spotify)"), tracks, err);
    });
}

void SpotifyWeb::playlists(std::function<void(QList<RemotePlaylist>, QString)> cb)
{
    if (!isConnected()) {
        cb({}, QStringLiteral("connectez votre compte Spotify (Paramètres)"));
        return;
    }
    libraryPage(true, std::make_shared<QList<RemotePlaylist>>(), 0, [cb](QList<RemotePlaylist> list, QString err) {
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        // « Titres likés » toujours proposés, en tête.
        const auto liked = std::find_if(list.begin(), list.end(), [](const RemotePlaylist &p) { return p.id == "liked"; });
        RemotePlaylist likedEntry{"liked", QStringLiteral("Titres likés"), "spotify:liked", -1};
        if (liked != list.end()) {
            likedEntry = *liked;
            list.erase(liked);
        }
        list.prepend(likedEntry);
        cb(list, {});
    });
}

void SpotifyWeb::libraryPage(bool flatten, std::shared_ptr<QList<RemotePlaylist>> acc, int offset,
                             std::function<void(QList<RemotePlaylist>, QString)> cb)
{
    // « flatten » : le contenu des dossiers est listé à plat (dossiers eux-mêmes ignorés).
    QJsonObject vars{
        {"filters", QJsonArray{"Playlists"}},
        {"order", QJsonValue::Null},
        {"textFilter", ""},
        {"features", QJsonArray{"LIKED_SONGS", "YOUR_EPISODES_V2", "PRERELEASES", "PRERELEASES_V2", "EVENTS"}},
        {"limit", 50},
        {"offset", offset},
        {"flatten", flatten},
        {"folderUri", QJsonValue::Null},
        {"includeFoldersWhenFlattening", true},
    };
    if (!flatten)
        vars["expandedFolders"] = QJsonArray();
    query("libraryV3", vars, true, [=, this](QJsonObject data, QString err) {
        const QJsonObject page = data["me"].toObject()["libraryV3"].toObject();
        if (err.isEmpty() && page["__typename"].toString() != QLatin1String("LibraryPage"))
            err = QStringLiteral("bibliothèque Spotify illisible (réponse inattendue)");
        if (!err.isEmpty()) {
            // Repli : liste « dépliée » comme l'affiche le lecteur web.
            if (flatten && offset == 0 && !err.contains(QLatin1String("expir")) && !err.contains(QLatin1String("non connecté")))
                libraryPage(false, acc, 0, cb);
            else
                cb({}, err);
            return;
        }
        int total = -1;
        *acc += pathfinderLibrary(page, &total);
        const int raw = int(page["items"].toArray().size());
        const int next = offset + raw;
        if (raw > 0 && (total < 0 || next < total) && next < 5000)
            libraryPage(flatten, acc, next, cb);
        else
            cb(*acc, {});
    });
}
