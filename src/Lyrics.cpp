#include "Lyrics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>

#include "Matcher.h"

QList<LyricLine> parseLrc(const QString &lrc)
{
    static const QRegularExpression stamp(QStringLiteral("^\\[(\\d+):(\\d{1,2}(?:[.:]\\d{1,3})?)\\]"));
    static const QRegularExpression offsetTag(QStringLiteral("^\\[offset:\\s*([+-]?\\d+)\\s*\\]"),
                                              QRegularExpression::CaseInsensitiveOption);
    double offset = 0;
    QList<LyricLine> lines;
    for (QString raw : lrc.split('\n')) {
        raw = raw.trimmed();
        if (const auto m = offsetTag.match(raw); m.hasMatch()) {
            offset = m.captured(1).toDouble() / 1000.0;  // positif = paroles plus tôt
            continue;
        }
        QList<double> times;
        for (auto m = stamp.match(raw); m.hasMatch(); m = stamp.match(raw)) {
            QString sec = m.captured(2);
            sec.replace(':', '.');
            times << m.captured(1).toDouble() * 60 + sec.toDouble();
            raw = raw.mid(m.capturedLength());
        }
        const QString text = raw.trimmed();
        for (double t : std::as_const(times))
            lines.append({std::max(0.0, t - offset), text});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const LyricLine &a, const LyricLine &b) { return a.time < b.time; });
    return lines;
}

int lyricIndexAt(const QList<LyricLine> &lines, double position)
{
    const auto it = std::upper_bound(lines.cbegin(), lines.cend(), position,
                                     [](double p, const LyricLine &l) { return p < l.time; });
    return int(it - lines.cbegin()) - 1;
}

LyricsClient::LyricsClient(QObject *parent) : QObject(parent)
{
    m_net.setTransferTimeout(15000);
}

void LyricsClient::search(const QUrlQuery &query, std::function<void(QJsonArray, bool, QString)> cb, int attempt)
{
    QUrl url(QStringLiteral("https://lrclib.net/api/search"));
    url.setQuery(query);
    QNetworkRequest req(url);
    // LRCLIB demande d'identifier l'application.
    req.setHeader(QNetworkRequest::UserAgentHeader, "Orpheus/1.0.0 (lecteur de musique de bureau)");
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, cb, query, attempt] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Service occupé (trop de demandes) : nouvelles tentatives espacées.
        if ((status == 429 || status >= 500) && attempt < 3) {
            QTimer::singleShot(3000 * (attempt + 1), this, [=, this] { search(query, cb, attempt + 1); });
            return;
        }
        if (r->error() != QNetworkReply::NoError || status != 200) {
            cb({}, false, r->errorString());
            return;
        }
        cb(QJsonDocument::fromJson(r->readAll()).array(), true, {});
    });
}

void LyricsClient::fetch(const QString &rawArtist, const QString &rawTitle, double duration, std::function<void(Result)> cb)
{
    QString artist = rawArtist, title = rawTitle;
    if (artist.trimmed().isEmpty()) {
        const auto split = Matcher::splitYoutubeTitle(title, {});
        artist = split.first;
        title = split.second.isEmpty() ? title : split.second;
    }
    artist = Matcher::primaryArtist(artist);
    title = Matcher::stripDecorations(title).trimmed();
    if (title.isEmpty()) {
        Result res;
        res.reached = true;
        cb(res);
        return;
    }
    const QString wantedTitle = Matcher::normalize(title);

    // Meilleure entrée : durée proche (±4 s) si connue, paroles synchronisées en priorité.
    auto pick = [duration, wantedTitle](const QJsonArray &arr, bool strictTitle) -> QJsonObject {
        QJsonObject best;
        double bestScore = -1;
        for (const QJsonValue &v : arr) {
            const QJsonObject e = v.toObject();
            const double diff = duration > 0 ? std::abs(e["duration"].toDouble() - duration) : 0;
            if (diff > 4)
                continue;
            if (strictTitle && Matcher::normalize(e["trackName"].toString()) != wantedTitle)
                continue;
            const bool synced = !e["syncedLyrics"].toString().isEmpty();
            const bool usable = synced || !e["plainLyrics"].toString().isEmpty() || e["instrumental"].toBool();
            if (!usable)
                continue;
            const double score = (synced ? 10 : 0) - diff;
            if (score > bestScore) {
                bestScore = score;
                best = e;
            }
        }
        return best;
    };
    auto finish = [cb](const QJsonObject &e, bool reached, const QString &err) {
        Result res;
        res.reached = reached;
        res.error = err;
        if (!e.isEmpty()) {
            res.found = true;
            res.instrumental = e["instrumental"].toBool();
            res.synced = e["syncedLyrics"].toString();
            res.plain = e["plainLyrics"].toString();
        }
        cb(res);
    };

    QUrlQuery precise;
    precise.addQueryItem("track_name", title);
    if (!artist.isEmpty())
        precise.addQueryItem("artist_name", artist);
    search(precise, [=, this](QJsonArray arr, bool reached, QString err) {
        if (!reached) {
            finish({}, false, err);
            return;
        }
        if (const QJsonObject e = pick(arr, false); !e.isEmpty()) {
            finish(e, true, {});
            return;
        }
        // Repli : recherche libre (artiste écrit autrement, titre YouTube approximatif).
        QUrlQuery loose;
        loose.addQueryItem("q", (artist + ' ' + title).trimmed());
        search(loose, [=](QJsonArray arr2, bool reached2, QString err2) {
            if (const QJsonObject e = pick(arr2, duration <= 0); !e.isEmpty() || !reached2) {
                finish(e, reached2, err2);
                return;
            }
            // Durée trop différente (clip avec introduction, version longue…) mais même
            // titre et même artiste : paroles non synchronisées, le minutage serait faux.
            const QString wantedArtist = Matcher::normalize(artist);
            for (const QJsonArray &list : {arr, arr2})
                for (const QJsonValue &v : list) {
                    QJsonObject e = v.toObject();
                    if (Matcher::normalize(e["trackName"].toString()) != wantedTitle
                        || Matcher::normalize(Matcher::primaryArtist(e["artistName"].toString())) != wantedArtist)
                        continue;
                    QString plain = e["plainLyrics"].toString();
                    if (plain.isEmpty()) {
                        QStringList texts;
                        for (const LyricLine &l : parseLrc(e["syncedLyrics"].toString()))
                            texts << l.text;
                        plain = texts.join('\n');
                    }
                    if (plain.trimmed().isEmpty())
                        continue;
                    e["syncedLyrics"] = QString();
                    e["plainLyrics"] = plain;
                    finish(e, true, {});
                    return;
                }
            finish({}, true, {});
        });
    });
}
