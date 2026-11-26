#include "GenreService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "Matcher.h"

namespace {

constexpr qint64 kRetryMissingSecs = 7 * 86400;
const char *kUa = "Orpheus/1.2 (+https://github.com/killex8569/Orpheus)";

void addStyle(QStringList &out, QSet<QString> &seen, const QString &style)
{
    const QString key = style.toLower();
    if (!style.isEmpty() && !seen.contains(key)) {
        seen.insert(key);
        out << style;
    }
}

bool containsWord(const QString &haystack, const QString &needle)
{
    // `haystack` vient de Matcher::normalize (minuscules, alphanumériques séparés par des espaces).
    const QString paddedHay = ' ' + haystack + ' ';
    return paddedHay.contains(' ' + needle + ' ');
}

bool containsAnyWord(const QString &haystack, std::initializer_list<const char *> words)
{
    for (const char *w : words)
        if (containsWord(haystack, QString::fromUtf8(w)))
            return true;
    return false;
}

bool hasJapaneseChars(const QString &text)
{
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if ((u >= 0x3040 && u <= 0x30FF) || (u >= 0x4E00 && u <= 0x9FFF))
            return true;
    }
    return false;
}

bool titleRoughlyMatches(const QString &expectedArtist, const QString &expectedTitle, const QString &gotArtist,
                         const QString &gotTitle)
{
    const QString ea = Matcher::normalize(Matcher::primaryArtist(expectedArtist));
    const QString et = Matcher::normalize(Matcher::stripDecorations(expectedTitle));
    const QString ga = Matcher::normalize(gotArtist);
    const QString gt = Matcher::normalize(gotTitle);
    if (et.isEmpty() || gt.isEmpty())
        return false;
    const bool titleOk = gt.contains(et) || et.contains(gt);
    if (!titleOk) {
        const QStringList words = et.split(' ', Qt::SkipEmptyParts);
        int matched = 0;
        for (const QString &w : words)
            if (w.size() >= 3 && gt.contains(w))
                ++matched;
        if (words.isEmpty() || matched * 2 < words.size())
            return false;
    }
    if (ea.isEmpty() || ga.isEmpty())
        return true;
    return ga.contains(ea) || ea.contains(ga) || titleOk;
}

}  // namespace

GenreService::GenreService(Database *db, QObject *parent) : QObject(parent), m_db(db)
{
    m_net.setTransferTimeout(10000);
}

QStringList GenreService::predefinedStyles()
{
    return {
        QStringLiteral("Électro"),
        QStringLiteral("Hardcore"),
        QStringLiteral("Phonk"),
        QStringLiteral("Pop"),
        QStringLiteral("Rap / Hip-Hop"),
        QStringLiteral("Rock"),
        QStringLiteral("Punk"),
        QStringLiteral("Metal"),
        QStringLiteral("Chill / Lo-Fi"),
        QStringLiteral("Gaming / OST"),
        QStringLiteral("J-Pop / Anime"),
        QStringLiteral("R&B / Soul"),
        QStringLiteral("Jazz / Classique"),
        QStringLiteral("Énergique"),
        QStringLiteral("Motivante"),
        QStringLiteral("Agressive"),
        QStringLiteral("Rapide"),
        QStringLiteral("Obscur"),
        QStringLiteral("Beat"),
    };
}

QStringList GenreService::inferFromMetadata(const QString &title, const QString &artist, const QString &album,
                                            const QStringList &playlistNames)
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString &s) { addStyle(out, seen, s); };

    const QString rawAll = title + ' ' + artist + ' ' + album;
    const QString normMain = Matcher::normalize(title + ' ' + artist + ' ' + album);
    const QString normPlaylists = Matcher::normalize(playlistNames.join(' '));
    const QString normAll = normMain + ' ' + normPlaylists;

    if (hasJapaneseChars(rawAll)
        || containsAnyWord(normAll, {"anime", "j pop", "jpop", "k pop", "kpop", "vocaloid", "utaite", "yoasobi",
                                     "creepy nuts", "kenshi yonezu", "tatsuya kitani", "fujii kaze", "frieren"})) {
        add(QStringLiteral("J-Pop / Anime"));
    }

    if (containsAnyWord(normAll, {"hardcore", "hardtekk", "freakstekk", "frenchcore", "hardstyle", "uptempo", "gabber",
                                  "rawstyle", "speedcore", "tekk", "hard dance", "jumpstyle"})) {
        add(QStringLiteral("Hardcore"));
        add(QStringLiteral("Énergique"));
        add(QStringLiteral("Agressive"));
        add(QStringLiteral("Rapide"));
    }

    if (containsAnyWord(normAll, {"phonk", "montagem", "automotivo", "brazilian funk", "vazio funk", "drift phonk",
                                  "cowbell"})) {
        add(QStringLiteral("Phonk"));
        add(QStringLiteral("Énergique"));
        add(QStringLiteral("Obscur"));
    } else if (containsWord(normMain, QStringLiteral("funk")) && !containsWord(normMain, QStringLiteral("daft punk"))) {
        add(QStringLiteral("Phonk"));
        add(QStringLiteral("Énergique"));
    }

    if (containsAnyWord(normAll, {"lofi", "lo fi", "chill", "dormir", "sleep", "relax", "ambient", "study", "calm",
                                  "music box", "acoustic", "piano"})) {
        add(QStringLiteral("Chill / Lo-Fi"));
    }

    if (containsAnyWord(normAll, {"ost", "soundtrack", "original soundtrack", "video game", "gaming", "expedition 33",
                                  "clair obscur", "frieren", "elden ring", "hollow knight", "undertale", "deltarune",
                                  "cyberpunk", "zelda", "minecraft", "persona", "arcane", "boss theme"})) {
        add(QStringLiteral("Gaming / OST"));
    }

    if (containsAnyWord(normAll, {"electro", "electronica", "electronic", "edm", "house", "techno", "trance", "dubstep",
                                  "drum and bass", "dnb", "synthwave", "retrowave", "vaporwave", "hyperpop",
                                  "eurobeat", "bass house", "future bass"})) {
        add(QStringLiteral("Électro"));
        add(QStringLiteral("Énergique"));
        if (containsAnyWord(normAll, {"drum and bass", "dnb", "eurobeat", "hyperpop"}))
            add(QStringLiteral("Rapide"));
    }

    if (containsAnyWord(normAll, {"beat", "beats", "type beat", "instrumental"}))
        add(QStringLiteral("Beat"));

    if (containsAnyWord(normAll, {"punk", "pop punk", "post punk", "skate punk"})) {
        add(QStringLiteral("Punk"));
        add(QStringLiteral("Énergique"));
    }

    if (containsAnyWord(normAll, {"metal", "heavy metal", "metalcore", "deathcore", "nu metal", "thrash", "djent"})) {
        add(QStringLiteral("Metal"));
        add(QStringLiteral("Agressive"));
        add(QStringLiteral("Énergique"));
    }

    if (containsAnyWord(normAll, {"rock", "indie rock", "hard rock", "alt rock", "grunge"}))
        add(QStringLiteral("Rock"));

    if (containsAnyWord(normAll, {"rap", "hip hop", "hiphop", "trap", "drill", "boom bap"})) {
        add(QStringLiteral("Rap / Hip-Hop"));
        if (containsWord(normAll, QStringLiteral("drill")))
            add(QStringLiteral("Agressive"));
    }

    if (containsAnyWord(normAll, {"pop", "synthpop", "electropop", "dance pop", "indie pop"}))
        add(QStringLiteral("Pop"));

    if (containsAnyWord(normMain, {"slowed", "dark", "obscur", "shadow", "nightmare", "demon", "villain", "decay",
                                   "void", "abyss", "ghost", "cursed", "hell"})) {
        add(QStringLiteral("Obscur"));
    }

    if (containsAnyWord(normAll, {"workout", "gym", "fitness", "motivation", "motivante", "motivant", "epic", "power",
                                  "unstoppable", "victory", "champion"})) {
        add(QStringLiteral("Motivante"));
        add(QStringLiteral("Énergique"));
    }

    if (containsAnyWord(normMain, {"sped up", "nightcore", "fast", "turbo", "rush", "overdrive"})) {
        add(QStringLiteral("Rapide"));
        add(QStringLiteral("Énergique"));
    }

    if (containsAnyWord(normMain, {"rage", "aggressive", "agressive", "agressif", "fury", "chaos", "slayer"})) {
        add(QStringLiteral("Agressive"));
        add(QStringLiteral("Énergique"));
    }

    return out;
}

QStringList GenreService::mapRawGenres(const QStringList &rawGenres)
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString &s) { addStyle(out, seen, s); };

    for (const QString &raw : rawGenres) {
        const QString g = Matcher::normalize(raw);
        if (g.isEmpty() || g == QLatin1String("music") || g == QLatin1String("musique"))
            continue;

        if (g.contains(QLatin1String("hardcore")) || g.contains(QLatin1String("hard dance"))
            || g.contains(QLatin1String("frenchcore")) || g.contains(QLatin1String("hardstyle"))
            || g.contains(QLatin1String("gabber"))) {
            add(QStringLiteral("Hardcore"));
            add(QStringLiteral("Énergique"));
            add(QStringLiteral("Agressive"));
            add(QStringLiteral("Rapide"));
        }
        if (g.contains(QLatin1String("electro")) || g.contains(QLatin1String("dance")) || g.contains(QLatin1String("house"))
            || g.contains(QLatin1String("techno")) || g.contains(QLatin1String("trance"))
            || g.contains(QLatin1String("dubstep")) || g.contains(QLatin1String("edm"))) {
            add(QStringLiteral("Électro"));
            add(QStringLiteral("Énergique"));
        }
        if (g.contains(QLatin1String("hip hop")) || g.contains(QLatin1String("rap")) || g.contains(QLatin1String("trap"))) {
            add(QStringLiteral("Rap / Hip-Hop"));
        }
        if (g.contains(QLatin1String("j pop")) || g.contains(QLatin1String("jpop")) || g.contains(QLatin1String("anime"))
            || g.contains(QLatin1String("k pop")) || g.contains(QLatin1String("kpop"))
            || g.contains(QLatin1String("kayokyoku"))) {
            add(QStringLiteral("J-Pop / Anime"));
        }
        if (g.contains(QLatin1String("pop")))
            add(QStringLiteral("Pop"));
        if (g.contains(QLatin1String("punk"))) {
            add(QStringLiteral("Punk"));
            add(QStringLiteral("Énergique"));
        }
        if (g.contains(QLatin1String("metal"))) {
            add(QStringLiteral("Metal"));
            add(QStringLiteral("Agressive"));
            add(QStringLiteral("Énergique"));
        }
        if (g.contains(QLatin1String("rock")) || g.contains(QLatin1String("alternative")) || g.contains(QLatin1String("indie")))
            add(QStringLiteral("Rock"));
        if (g.contains(QLatin1String("soundtrack")) || g.contains(QLatin1String("films"))
            || g.contains(QLatin1String("jeux video")) || g.contains(QLatin1String("musiques de films"))
            || g.contains(QLatin1String("video game")) || g.contains(QLatin1String("score"))) {
            add(QStringLiteral("Gaming / OST"));
        }
        if (g.contains(QLatin1String("r b")) || g.contains(QLatin1String("rnb")) || g.contains(QLatin1String("soul")))
            add(QStringLiteral("R&B / Soul"));
        if (g.contains(QLatin1String("jazz")) || g.contains(QLatin1String("classical"))
            || g.contains(QLatin1String("classique")) || g.contains(QLatin1String("blues"))) {
            add(QStringLiteral("Jazz / Classique"));
        }
        if (g.contains(QLatin1String("ambient")) || g.contains(QLatin1String("chill"))
            || g.contains(QLatin1String("lo fi")) || g.contains(QLatin1String("lofi"))
            || g.contains(QLatin1String("new age")) || g.contains(QLatin1String("easy listening"))) {
            add(QStringLiteral("Chill / Lo-Fi"));
        }
    }

    return out;
}

void GenreService::enqueuePlaylist(qint64 playlistId, bool applyHeuristicsFirst)
{
    if (playlistId <= 0)
        return;
    const QList<Track> tracks = m_db->playlistTracks(playlistId);
    const qint64 retryBefore = nowSecs() - kRetryMissingSecs;
    QList<qint64> toQueue;

    for (const Track &t : tracks) {
        if (m_db->hasUserStyles(t.id))
            continue;
        if (applyHeuristicsFirst && t.styles.isEmpty()) {
            const QStringList inferred = inferFromMetadata(t.title, t.artist, t.album, m_db->playlistNamesForTrack(t.id));
            if (!inferred.isEmpty()) {
                m_db->setTrackStyles(t.id, inferred, false);
                emit trackStylesChanged(t.id);
            }
        }
        const auto status = m_db->styleFetchStatus(t.id);
        const bool needsFetch = !status || (status->first == QLatin1String("missing") && status->second < retryBefore);
        if (needsFetch && !m_queue.contains(t.id))
            toQueue.append(t.id);
    }

    if (!toQueue.isEmpty()) {
        // Priorité à la playlist ouverte (placée devant les autres morceaux en attente).
        for (qsizetype i = toQueue.size() - 1; i >= 0; --i)
            m_queue.prepend(toQueue[i]);
        emit statusChanged();
        pump();
    }
}

void GenreService::enqueueTrack(qint64 trackId, bool force)
{
    if (trackId <= 0)
        return;
    if (!force && m_db->hasUserStyles(trackId))
        return;
    const auto t = m_db->track(trackId);
    if (!t)
        return;
    if (!force && t->styles.isEmpty()) {
        const QStringList inferred = inferFromMetadata(t->title, t->artist, t->album, m_db->playlistNamesForTrack(trackId));
        if (!inferred.isEmpty()) {
            m_db->setTrackStyles(trackId, inferred, false);
            emit trackStylesChanged(trackId);
        }
    }
    if (!force) {
        const auto status = m_db->styleFetchStatus(trackId);
        if (status && (status->first == QLatin1String("found") || nowSecs() - status->second < kRetryMissingSecs))
            return;
    }
    m_queue.removeAll(trackId);
    if (force)
        m_queue.prepend(trackId);
    else
        m_queue.append(trackId);
    emit statusChanged();
    pump();
}

void GenreService::enqueueMissing()
{
    const QList<Track> missing = m_db->tracksNeedingStyles(0, 300);
    for (const Track &t : missing)
        if (!m_queue.contains(t.id))
            m_queue.append(t.id);
    if (!m_queue.isEmpty()) {
        emit statusChanged();
        pump();
    }
}

void GenreService::pump()
{
    if (m_busy || m_queue.isEmpty()) {
        emit statusChanged();
        return;
    }
    const qint64 id = m_queue.takeFirst();
    const auto t = m_db->track(id);
    if (!t) {
        pump();
        return;
    }
    m_busy = true;
    emit statusChanged();

    fetchItunes(*t, [this, track = *t](QStringList genres, bool reachedItunes) {
        if (!genres.isEmpty()) {
            finishTrack(track, genres, true);
            return;
        }
        fetchDeezer(track, [this, track, reachedItunes](QStringList dzGenres, bool reachedDeezer) {
            finishTrack(track, dzGenres, reachedItunes || reachedDeezer);
        });
    });
}

void GenreService::finishTrack(const Track &t, const QStringList &apiGenres, bool reached)
{
    m_busy = false;
    if (!reached) {
        if (++m_failStreak < 5)
            m_queue.append(t.id);
        const int waitMs = m_failStreak >= 5 ? 5 * 60 * 1000 : 5000;
        if (m_failStreak >= 5)
            m_failStreak = 0;
        emit statusChanged();
        QTimer::singleShot(waitMs, this, &GenreService::pump);
        return;
    }

    m_failStreak = 0;
    QStringList merged = mapRawGenres(apiGenres);
    QSet<QString> seen;
    for (const QString &s : std::as_const(merged))
        seen.insert(s.toLower());

    const QStringList inferred = inferFromMetadata(t.title, t.artist, t.album, m_db->playlistNamesForTrack(t.id));
    for (const QString &s : inferred)
        addStyle(merged, seen, s);

    const bool userOwned = m_db->hasUserStyles(t.id);
    if (!userOwned) {
        m_db->setTrackStyles(t.id, merged, false);
        m_db->saveStyleFetch(t.id, merged.isEmpty() ? QStringLiteral("missing") : QStringLiteral("found"),
                             apiGenres.join(QStringLiteral(", ")));
    } else if (!merged.isEmpty()) {
        // Si l'utilisateur a déclenché « Auto-détecter » manuellement depuis le dialogue,
        // on enrichit sans écraser les styles qu'il avait déjà choisis.
        QStringList combined = m_db->trackStyles(t.id);
        QSet<QString> existing;
        for (const QString &s : std::as_const(combined))
            existing.insert(s.toLower());
        for (const QString &s : std::as_const(merged))
            addStyle(combined, existing, s);
        m_db->setTrackStyles(t.id, combined, true);
    }

    emit trackStylesChanged(t.id);
    emit statusChanged();
    QTimer::singleShot(500, this, &GenreService::pump);
}

void GenreService::fetchItunes(const Track &t, std::function<void(QStringList, bool)> cb)
{
    const QString artist = Matcher::primaryArtist(t.artist);
    const QString title = Matcher::stripDecorations(t.title);
    const QString term = (artist + ' ' + title).simplified();
    if (term.isEmpty()) {
        cb({}, true);
        return;
    }

    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("term"), term);
    q.addQueryItem(QStringLiteral("entity"), QStringLiteral("song"));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("3"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [r, t, cb] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || (r->error() != QNetworkReply::NoError && status == 0)) {
            cb({}, false);
            return;
        }
        const QJsonArray results = QJsonDocument::fromJson(r->readAll()).object()["results"].toArray();
        QStringList genres;
        for (const QJsonValue &v : results) {
            const QJsonObject o = v.toObject();
            const QString ga = o["artistName"].toString();
            const QString gt = o["trackName"].toString();
            const QString g = o["primaryGenreName"].toString().trimmed();
            if (g.isEmpty() || g.compare(QLatin1String("Music"), Qt::CaseInsensitive) == 0)
                continue;
            if (titleRoughlyMatches(t.artist, t.title, ga, gt)) {
                genres << g;
                break;
            }
        }
        cb(genres, true);
    });
}

void GenreService::fetchDeezer(const Track &t, std::function<void(QStringList, bool)> cb)
{
    const QString artist = Matcher::primaryArtist(t.artist);
    const QString title = Matcher::stripDecorations(t.title);
    const QString term = (artist + ' ' + title).simplified();
    if (term.isEmpty()) {
        cb({}, true);
        return;
    }

    QUrl url(QStringLiteral("https://api.deezer.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), term);
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("3"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUa);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, t, cb] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || (r->error() != QNetworkReply::NoError && status == 0)) {
            cb({}, false);
            return;
        }
        const QJsonArray data = QJsonDocument::fromJson(r->readAll()).object()["data"].toArray();
        qint64 albumId = 0;
        for (const QJsonValue &v : data) {
            const QJsonObject o = v.toObject();
            const QString ga = o["artist"].toObject()["name"].toString();
            const QString gt = o["title"].toString();
            if (titleRoughlyMatches(t.artist, t.title, ga, gt)) {
                albumId = o["album"].toObject()["id"].toVariant().toLongLong();
                if (albumId > 0)
                    break;
            }
        }
        if (albumId <= 0) {
            cb({}, true);
            return;
        }

        QNetworkRequest areq(QUrl(QStringLiteral("https://api.deezer.com/album/%1").arg(albumId)));
        areq.setHeader(QNetworkRequest::UserAgentHeader, kUa);
        QNetworkReply *ar = m_net.get(areq);
        connect(ar, &QNetworkReply::finished, this, [ar, cb] {
            ar->deleteLater();
            const int astatus = ar->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (astatus == 429 || (ar->error() != QNetworkReply::NoError && astatus == 0)) {
                cb({}, false);
                return;
            }
            QStringList genres;
            const QJsonArray arr = QJsonDocument::fromJson(ar->readAll()).object()["genres"].toObject()["data"].toArray();
            for (const QJsonValue &gv : arr) {
                const QString name = gv.toObject()["name"].toString().trimmed();
                if (!name.isEmpty())
                    genres << name;
            }
            cb(genres, true);
        });
    });
}
