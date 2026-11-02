#include "Matcher.h"

#include <QStringList>
#include <algorithm>
#include <cmath>

namespace Matcher {

namespace {

// Mots qui signalent une version différente de l'original.
const QStringList kVariantWords = {
    "live", "cover", "karaoke", "remix", "sped", "slowed", "nightcore", "8d", "instrumental", "reaction",
    "acoustic", "acapella", "reverb", "bass boosted", "lesson", "tutorial", "piano version", "extended",
    "mashup", "parody", "teaser", "trailer", "edit", "reinterprete", "documentaire", "interview",
};

// Bruit fréquent dans les titres YouTube, sans valeur pour l'identification.
const QStringList kNoise = {
    "official music video", "official video", "official audio", "official lyric video", "lyric video",
    "lyrics video", "music video", "visualizer", "visualiser", "audio officiel", "clip officiel",
    "video officielle", "official", "lyrics", "paroles", "audio", "hd", "hq", "4k", "mv", "clip",
};

QStringList tokens(const QString &s)
{
    return normalize(s).split(' ', Qt::SkipEmptyParts);
}

double coverage(const QStringList &needle, const QStringList &hay)
{
    if (needle.isEmpty())
        return 0;
    int hits = 0;
    for (const QString &w : needle)
        hits += hay.contains(w) ? 1 : 0;
    return double(hits) / needle.size();
}

bool containsPhrase(const QString &hay, const QString &phrase)
{
    return (' ' + hay + ' ').contains(' ' + phrase + ' ');
}

QString mainArtist(const QString &artist)
{
    return normalize(primaryArtist(artist));
}

}  // namespace

QString normalize(const QString &s)
{
    const QString decomposed = s.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_Enclosing
            || c.category() == QChar::Mark_SpacingCombining)
            continue;
        if (c == '&')
            out += QStringLiteral(" and ");
        else if (c == '$')
            out += 's';
        else if (c.isLetterOrNumber())
            out += c.toLower();
        else
            out += ' ';
    }
    return out.simplified();
}

QString primaryArtist(const QString &artist)
{
    const QString a = artist.toLower();
    qsizetype cut = a.size();
    for (const char *sep : {",", " & ", " x ", " feat", " ft.", " and ", ";", " / "}) {
        const qsizetype i = a.indexOf(QLatin1String(sep));
        if (i >= 0)
            cut = std::min(cut, i);
    }
    return artist.left(cut).trimmed();
}

QString stripDecorations(const QString &s)
{
    QString out;
    int depth = 0;
    for (const QChar c : s) {
        if (c == '(' || c == '[' || c == '{')
            ++depth;
        else if (c == ')' || c == ']' || c == '}')
            depth = std::max(0, depth - 1);
        else if (depth == 0)
            out += c;
    }
    const QString lower = out.toLower();
    qsizetype cut = out.size();
    for (const char *pat : {" feat.", " feat ", " ft.", " ft ", " featuring ", " prod.", " prod "}) {
        const qsizetype i = lower.indexOf(QLatin1String(pat));
        if (i >= 0)
            cut = std::min(cut, i);
    }
    QString r = out.left(cut).trimmed();
    while (!r.isEmpty() && (r.endsWith('-') || r.endsWith('|') || r.endsWith('/')))
        r.chop(1);
    return r.trimmed();
}

QPair<QString, QString> splitYoutubeTitle(const QString &title, const QString &channel)
{
    QString channelArtist = channel;
    if (channelArtist.endsWith(QLatin1String(" - Topic")))
        channelArtist.chop(8);
    if (channelArtist.endsWith(QLatin1String("VEVO")))
        channelArtist.chop(4);
    channelArtist = channelArtist.trimmed();
    for (const QString &sep : {QStringLiteral(" - "), QStringLiteral(" – "), QStringLiteral(" — "), QStringLiteral(" | ")}) {
        const qsizetype i = title.indexOf(sep);
        if (i > 0) {
            QString a = title.left(i).trimmed();
            QString t = title.mid(i + sep.size()).trimmed();
            // « Titre - Artiste » : la partie droite correspond à la chaîne → on inverse.
            const QString chan = normalize(channelArtist);
            if (!chan.isEmpty() && normalize(stripDecorations(t)) == chan && normalize(a) != chan)
                std::swap(a, t);
            t = stripDecorations(t);
            if (!a.isEmpty() && !t.isEmpty())
                return {a, t};
        }
    }
    return {channelArtist, stripDecorations(title)};
}

QString matchKey(const QString &artist, const QString &title)
{
    QString a = artist, t = title;
    if (a.trimmed().isEmpty()) {
        const auto split = splitYoutubeTitle(title, QString());
        a = split.first;
        t = split.second;
    }
    const QString nt = normalize(stripDecorations(t));
    if (nt.isEmpty())
        return {};
    return mainArtist(a) + '|' + nt;
}

double score(const QString &artist, const QString &title, double duration, const Candidate &c)
{
    const QStringList wantTitle = tokens(stripDecorations(title));
    const QStringList wantArtist = tokens(mainArtist(artist));
    const QStringList allArtist = tokens(artist);
    const QString candText = c.title + ' ' + c.channel;
    const QStringList candTokens = tokens(candText);
    const QString candNorm = normalize(candText);
    const QString queryNorm = normalize(artist + ' ' + title);

    const double titleCov = coverage(wantTitle, candTokens);
    const double artistCov = wantArtist.isEmpty() ? 0.5 : coverage(wantArtist, candTokens);
    double s = 0.55 * titleCov + 0.25 * artistCov;

    // Mots en trop dans le titre (hors bruit connu) : signe d'une autre version.
    QString candTitle = ' ' + normalize(c.title) + ' ';
    for (const QString &n : kNoise)
        candTitle.replace(' ' + n + ' ', QStringLiteral(" "));
    int extra = 0;
    for (const QString &w : candTitle.split(' ', Qt::SkipEmptyParts))
        if (!wantTitle.contains(w) && !wantArtist.contains(w) && !allArtist.contains(w))
            ++extra;
    s -= 0.02 * std::min(extra, 8);

    for (const QString &w : kVariantWords)
        if (containsPhrase(candNorm, w) && !containsPhrase(queryNorm, w))
            s -= 0.35;

    if (duration > 0 && c.duration > 0) {
        const double diff = std::abs(duration - c.duration);
        if (diff <= 3)
            s += 0.2;
        else if (diff <= 8)
            s += 0.1;
        else if (diff <= 20)
            s -= 0.05;
        else if (diff <= 60)
            s -= 0.25;
        else
            s -= 0.5;
    }
    // Les chaînes « Topic » publient l'audio officiel des distributeurs.
    if (c.channel.endsWith(QLatin1String(" - Topic")))
        s += 0.1;
    if (containsPhrase(normalize(c.title), QStringLiteral("audio")))
        s += 0.03;
    return s;
}

int best(const QString &artist, const QString &title, double duration, const QList<Candidate> &cands)
{
    int bestIdx = -1;
    double bestScore = AcceptThreshold;
    for (int i = 0; i < cands.size(); ++i) {
        const double sc = score(artist, title, duration, cands[i]);
        if (sc >= bestScore) {
            bestScore = sc;
            bestIdx = i;
        }
    }
    return bestIdx;
}

}  // namespace Matcher
