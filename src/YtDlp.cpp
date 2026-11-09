#include "YtDlp.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>
#include <memory>

#include "Matcher.h"
#include "Paths.h"

namespace {

QString str(const QJsonValue &v)
{
    if (v.isDouble())
        return QString::number(v.toDouble());
    const QString s = v.toString().trimmed();
    return s == QLatin1String("NA") ? QString() : s;
}

double num(const QJsonValue &v)
{
    return v.isDouble() ? v.toDouble() : v.toString().toDouble();
}

// Entrée d'une recherche ou d'une playlist « à plat ».
std::optional<Track> entryToTrack(const QJsonObject &e, Matcher::Candidate *raw = nullptr)
{
    const QString id = str(e["id"]);
    if (id.size() != 11)
        return std::nullopt;  // chaîne ou playlist dans des résultats de recherche
    const QString rawTitle = str(e["title"]);
    if (rawTitle == QLatin1String("[Deleted video]") || rawTitle == QLatin1String("[Private video]"))
        return std::nullopt;
    QString channel = str(e["channel"]);
    if (channel.isEmpty())
        channel = str(e["uploader"]);
    Track t;
    t.ytId = id;
    if (!str(e["artist"]).isEmpty() && !str(e["track"]).isEmpty()) {
        t.artist = str(e["artist"]);
        t.title = str(e["track"]);
    } else {
        const auto split = Matcher::splitYoutubeTitle(rawTitle, channel);
        t.artist = split.first;
        t.title = split.second.isEmpty() ? rawTitle : split.second;
    }
    t.duration = num(e["duration"]);
    t.thumbnail = thumbUrl(id);
    if (raw)
        *raw = {rawTitle, channel, t.duration};
    return t;
}

}  // namespace

QString ytdlpError(const QString &stderrText)
{
    const QStringList lines = stderrText.split('\n', Qt::SkipEmptyParts);
    QString line;
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (it->contains(QLatin1String("ERROR"))) {
            line = *it;
            break;
        }
    }
    if (line.isEmpty())
        line = lines.isEmpty() ? QStringLiteral("erreur inconnue") : lines.last();
    line = line.trimmed();
    if (line.startsWith(QLatin1String("ERROR:")))
        line = line.mid(6).trimmed();
    if (line.contains(QLatin1String("Sign in to confirm")))
        return QStringLiteral("YouTube demande une vérification : choisissez le navigateur connecté dans les paramètres");
    if (line.contains(QLatin1String("403")))
        return QStringLiteral("YouTube a refusé le téléchargement (403) — nouvel essai automatique");
    return line.left(300);
}

QString netscapeCookie(const QString &text, const QString &domain, const QString &name)
{
    QString best;
    qint64 bestExpiry = -1;
    for (QString line : text.split('\n')) {
        line.remove('\r');
        if (line.startsWith(QLatin1String("#HttpOnly_")))
            line = line.mid(10);
        else if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QStringList f = line.split('\t');
        if (f.size() < 7 || f[5] != name || f[6].trimmed().isEmpty())
            continue;
        QString host = f[0].toLower();
        if (host.startsWith('.'))
            host = host.mid(1);
        if (host != domain && !host.endsWith('.' + domain))
            continue;
        // Plusieurs entrées (sous-domaines) : la plus durable l'emporte.
        const qint64 expiry = f[4].toLongLong();
        if (expiry >= bestExpiry) {
            best = f[6].trimmed();
            bestExpiry = expiry;
        }
    }
    return best;
}

// Copie gérée : binaire officiel autonome propre à chaque système (pas besoin de Python
// sous Windows et macOS). Sous Linux, l'archive « yt-dlp » s'exécute avec python3.
#if defined(Q_OS_WIN)
static const QString kManagedName = QStringLiteral("yt-dlp.exe");
static const QString kManagedUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe");
#elif defined(Q_OS_MACOS)
static const QString kManagedName = QStringLiteral("yt-dlp");
static const QString kManagedUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos");
#else
static const QString kManagedName = QStringLiteral("yt-dlp");
static const QString kManagedUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp");
#endif

YtDlp::YtDlp(const QString &managedDir, QObject *parent) : QObject(parent), m_managedDir(managedDir)
{
    redetect();
}

void YtDlp::redetect()
{
    m_candidates.clear();
    auto add = [this](const QString &path) {
        if (path.isEmpty() || !QFileInfo(path).isExecutable())
            return;
        const QString real = QFileInfo(path).canonicalFilePath();
        for (const QString &c : std::as_const(m_candidates))
            if (QFileInfo(c).canonicalFilePath() == real)
                return;
        m_candidates << path;
    };
    // Priorité à la copie gérée par Orpheus (mise à jour automatiquement).
    add(m_managedDir + "/" + kManagedName);
    add(QStandardPaths::findExecutable("yt-dlp"));
    const QString home = QDir::homePath();
    for (const QString &dir : {home + "/.local/bin", QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")})
        add(dir + "/yt-dlp");
    if (!m_candidates.contains(m_bin))
        m_bin = m_candidates.value(0);
    // Moteur JavaScript exigé par YouTube : deno de préférence, sinon Node.js (paquet des
    // distributions, ex. Fedora, où deno n'existe pas).
    m_jsRuntime.clear();
    if (const QString deno = findExecutable("deno"); !deno.isEmpty())
        m_jsRuntime = "deno:" + deno;
    else if (const QString node = findExecutable("node"); !node.isEmpty())
        m_jsRuntime = "node:" + node;
}

void YtDlp::selectNewest(std::function<void(QString)> cb)
{
    redetect();
    if (m_candidates.isEmpty()) {
        m_bin.clear();
        cb({});
        return;
    }
    struct State {
        int pending = 0;
        QString best, bestVersion;
    };
    auto st = std::make_shared<State>();
    st->pending = int(m_candidates.size());
    for (const QString &bin : std::as_const(m_candidates)) {
        auto *p = new QProcess(this);
        auto done = std::make_shared<bool>(false);
        auto finish = [this, p, bin, st, cb, done](bool ok) {
            if (*done)
                return;
            *done = true;
            const QString v = ok ? QString::fromUtf8(p->readAllStandardOutput()).trimmed() : QString();
            p->deleteLater();
            if (!v.isEmpty() && (st->best.isEmpty()
                                 || QVersionNumber::fromString(v) > QVersionNumber::fromString(st->bestVersion))) {
                st->best = bin;
                st->bestVersion = v;
            }
            if (--st->pending == 0) {
                if (!st->best.isEmpty()) {
                    if (st->best != m_bin)
                        qInfo() << "yt-dlp retenu :" << st->best << st->bestVersion;
                    m_bin = st->best;
                }
                cb(st->bestVersion);
            }
        };
        connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus s) {
            finish(code == 0 && s == QProcess::NormalExit);
        });
        connect(p, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                finish(false);
        });
        auto *timer = new QTimer(p);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
        p->start(bin, {"--version"});
        timer->start(15000);
    }
}

bool YtDlp::isManaged() const
{
    return m_bin == m_managedDir + "/" + kManagedName;
}

QStringList YtDlp::baseArgs() const
{
    QStringList a{"--no-warnings", "--ignore-config", "--socket-timeout", "20"};
    // YouTube exige un moteur JavaScript pour déchiffrer les flux.
    if (!m_jsRuntime.isEmpty())
        a << "--js-runtimes" << m_jsRuntime;
    if (!m_cookiesBrowser.isEmpty() && !m_cookiesBroken)
        a << "--cookies-from-browser" << m_cookiesBrowser;
    return a;
}

bool YtDlp::dropBrokenCookies(const QString &stderrText)
{
    if (m_cookiesBrowser.isEmpty() || m_cookiesBroken)
        return false;
    // Erreurs de lecture des cookies uniquement : « Sign in to confirm… » mentionne aussi
    // --cookies-from-browser mais demande au contraire des cookies.
    static const QRegularExpression re(
        QStringLiteral("cookies? database|DPAPI|failed to decrypt|could not copy .*cookie|"
                       "failed to load cookies|cookies? (file|store) .*(locked|permission)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!re.match(stderrText).hasMatch())
        return false;
    qWarning() << "cookies de" << m_cookiesBrowser << "illisibles, on continue sans :" << ytdlpError(stderrText);
    m_cookiesBroken = true;
    emit cookiesUnavailable(m_cookiesBrowser);
    return true;
}

void YtDlp::runJson(const QStringList &args, int timeoutMs, std::function<void(QJsonDocument, QString)> cb)
{
    if (m_bin.isEmpty()) {
        cb({}, QStringLiteral("yt-dlp introuvable : installez-le ou utilisez « Mettre à jour yt-dlp » dans les paramètres"));
        return;
    }
    auto *p = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    auto timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [this, p, args, timeoutMs, cb, finished](int code, QProcess::ExitStatus st) {
        if (*finished)
            return;
        *finished = true;
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        if (st != QProcess::NormalExit) {
            cb({}, QStringLiteral("yt-dlp ne répond pas (délai dépassé)"));
            return;
        }
        if (code != 0) {
            // Cookies illisibles : même requête sans eux (recherche, import Spotify…),
            // sauf pour ce qui exige le compte YouTube.
            const bool needsAccount = args.join(' ').contains(QLatin1String("feed/playlists"))
                                      || args.join(' ').contains(QLatin1String("list=LM"));
            if (dropBrokenCookies(err) && !needsAccount) {
                runJson(args, timeoutMs, cb);
                return;
            }
            cb({}, ytdlpError(err));
            return;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &pe);
        if (pe.error != QJsonParseError::NoError)
            cb({}, QStringLiteral("réponse yt-dlp illisible"));
        else
            cb(doc, {});
    });
    connect(p, &QProcess::errorOccurred, this, [p, cb, finished](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *finished)
            return;
        *finished = true;
        p->deleteLater();
        cb({}, QStringLiteral("impossible de lancer yt-dlp"));
    });
    p->start(m_bin, baseArgs() + args);
    timer->start(timeoutMs);
}

void YtDlp::version(std::function<void(QString)> cb)
{
    if (m_bin.isEmpty()) {
        cb({});
        return;
    }
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, cb] {
        cb(QString::fromUtf8(p->readAllStandardOutput()).trimmed());
        p->deleteLater();
    });
    // Sans démarrage, finished n'est jamais émis.
    connect(p, &QProcess::errorOccurred, this, [p, cb](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            cb({});
        }
    });
    p->start(m_bin, {"--version"});
}

void YtDlp::searchRaw(const QString &query, int limit, std::function<void(QList<Track>, QList<Matcher::Candidate>, QString)> cb)
{
    runJson({"--flat-playlist", "-J", QStringLiteral("ytsearch%1:%2").arg(limit).arg(query)}, 40000,
            [cb](QJsonDocument doc, QString err) {
                QList<Track> tracks;
                QList<Matcher::Candidate> raws;
                for (const QJsonValue &v : doc.object()["entries"].toArray()) {
                    Matcher::Candidate raw;
                    if (auto t = entryToTrack(v.toObject(), &raw)) {
                        tracks.append(*t);
                        raws.append(raw);
                    }
                }
                cb(tracks, raws, err);
            });
}

void YtDlp::search(const QString &query, int limit, TracksCb cb)
{
    searchRaw(query, limit, [cb](QList<Track> t, QList<Matcher::Candidate>, QString err) { cb(t, err); });
}

void YtDlp::findMatch(const QString &artist, const QString &title, double duration,
                      std::function<void(std::optional<Track>, QString)> cb)
{
    QStringList queries{artist + " - " + title};
    if (!artist.isEmpty())
        queries << artist + " " + title + " audio";
    // Essaie les requêtes l'une après l'autre jusqu'à une correspondance fiable.
    // La fonction ne se référence que faiblement (sinon cycle → fuite) ; chaque recherche
    // en cours la garde en vie.
    auto attempt = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weak = attempt;
    *attempt = [this, queries, artist, title, duration, cb, weak](int i) {
        if (i >= queries.size()) {
            cb(std::nullopt, {});
            return;
        }
        // Le score compare aux titres bruts des vidéos pour repérer live, covers, remix…
        searchRaw(queries[i], 8, [=, attempt = weak.lock()](QList<Track> tracks, QList<Matcher::Candidate> cands, QString err) {
            if (!err.isEmpty()) {
                cb(std::nullopt, err);
                return;
            }
            const int idx = Matcher::best(artist, title, duration, cands);
            if (idx >= 0)
                cb(tracks[idx], {});
            else
                (*attempt)(i + 1);
        });
    };
    (*attempt)(0);
}

void YtDlp::playlist(const QString &url, std::function<void(QString, QList<Track>, QString)> cb)
{
    runJson({"--flat-playlist", "-J", "--yes-playlist", url}, 600000, [cb](QJsonDocument doc, QString err) {
        const QJsonObject o = doc.object();
        QList<Track> tracks;
        for (const QJsonValue &v : o["entries"].toArray())
            if (auto t = entryToTrack(v.toObject()))
                tracks.append(*t);
        QString title = str(o["title"]);
        cb(title.isEmpty() ? QStringLiteral("Playlist YouTube") : title, tracks, err);
    });
}

void YtDlp::accountPlaylists(std::function<void(QList<RemotePlaylist>, QString)> cb)
{
    if (m_cookiesBrowser.isEmpty()) {
        cb({}, QStringLiteral("choisissez le navigateur connecté à YouTube dans les paramètres"));
        return;
    }
    runJson({"--flat-playlist", "-J", "https://www.youtube.com/feed/playlists"}, 90000, [cb](QJsonDocument doc, QString err) {
        QList<RemotePlaylist> out{
            {"LM", QStringLiteral("Titres « J'aime » (YouTube Music)"), "https://music.youtube.com/playlist?list=LM", -1},
            {"LL", QStringLiteral("Vidéos « J'aime »"), "https://www.youtube.com/playlist?list=LL", -1},
        };
        if (!err.isEmpty()) {
            cb({}, err);
            return;
        }
        for (const QJsonValue &v : doc.object()["entries"].toArray()) {
            const QJsonObject e = v.toObject();
            const QString id = str(e["id"]);
            if (id.isEmpty() || id == "LL" || id == "LM")
                continue;
            RemotePlaylist p;
            p.id = id;
            p.title = str(e["title"]).isEmpty() ? id : str(e["title"]);
            p.url = str(e["url"]).isEmpty() ? "https://www.youtube.com/playlist?list=" + id : str(e["url"]);
            p.count = e["playlist_count"].isDouble() ? e["playlist_count"].toInt() : -1;
            out.append(p);
        }
        cb(out, {});
    });
}

void YtDlp::resolveStream(const QString &ytId, std::function<void(QString, QString, QString)> cb)
{
    runJson({"-J", "--no-playlist", "-f", "bestaudio[acodec=opus]/bestaudio/best", watchUrl(ytId)}, 60000,
            [cb](QJsonDocument doc, QString err) {
                if (!err.isEmpty()) {
                    cb({}, {}, err);
                    return;
                }
                const QJsonObject o = doc.object();
                QString url = str(o["url"]);
                if (url.isEmpty())
                    url = str(o["requested_formats"].toArray().at(0).toObject()["url"]);
                const QString ua = str(o["http_headers"].toObject()["User-Agent"]);
                cb(url, ua, url.isEmpty() ? QStringLiteral("aucun flux audio disponible") : QString());
            });
}

QProcess *YtDlp::download(const QString &ytId, const QString &libraryDir, const QString &coversDir,
                          std::function<void(double)> progress, std::function<void(DownloadResult, QString)> done)
{
    if (m_bin.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done({}, QStringLiteral("yt-dlp introuvable")); });
        return nullptr;
    }
    QStringList args = baseArgs();
    // --print implique --quiet : --progress rétablit la progression.
    args << "--no-playlist" << "--newline" << "--progress"
         // Opus d'abord (le meilleur codec de YouTube), sinon le meilleur flux audio.
         << "-f" << "bestaudio[acodec=opus]/bestaudio/best"
         // Extraction sans conversion : aucun réencodage, aucune perte ajoutée.
         << "-x" << "--embed-metadata"
         << "--write-thumbnail" << "--convert-thumbnails" << "jpg"
         << "--retries" << "3" << "--fragment-retries" << "3"
         << "--progress-template" << "download:ORPHEUS_PROGRESS %(progress._percent_str)s"
         << "--print" << "after_move:ORPHEUS_FILE %(filepath)s"
         << "--print" << "after_move:ORPHEUS_META %(.{title,track,artist,album,duration,abr,acodec,channel,uploader})j"
         << "-o" << (libraryDir + "/%(id)s.%(ext)s")
         << "-o" << ("thumbnail:" + coversDir + "/%(id)s.%(ext)s")
         << watchUrl(ytId);

    auto *p = new QProcess(this);
    auto result = std::make_shared<DownloadResult>();
    auto buffer = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });

    connect(p, &QProcess::readyReadStandardOutput, this, [p, result, buffer, progress] {
        buffer->append(p->readAllStandardOutput());
        qsizetype nl;
        while ((nl = buffer->indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(buffer->left(nl)).trimmed();
            buffer->remove(0, nl + 1);
            if (line.startsWith(QLatin1String("ORPHEUS_PROGRESS"))) {
                QString pct = line.mid(16).trimmed();
                pct.remove('%');
                bool ok = false;
                const double v = pct.toDouble(&ok);
                if (ok)
                    progress(qBound(0.0, v, 100.0));
            } else if (line.startsWith(QLatin1String("ORPHEUS_FILE "))) {
                result->path = line.mid(13).trimmed();
            } else if (line.startsWith(QLatin1String("ORPHEUS_META "))) {
                const QJsonObject m = QJsonDocument::fromJson(line.mid(13).toUtf8()).object();
                // Seules les vraies métadonnées musicales remplacent le titre affiché
                // (sinon on garderait « Artiste - Titre (Official Video) »).
                result->title = str(m["track"]);
                result->artist = result->title.isEmpty() ? QString() : str(m["artist"]);
                result->album = str(m["album"]);
                result->duration = num(m["duration"]);
                result->bitrate = num(m["abr"]);
                result->codec = str(m["acodec"]);
                result->rawTitle = str(m["title"]);
                result->channel = str(m["channel"]).isEmpty() ? str(m["uploader"]) : str(m["channel"]);
            }
        }
    });
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus st) {
        if (*finished)
            return;
        *finished = true;
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        if (st != QProcess::NormalExit) {
            done({}, QStringLiteral("téléchargement interrompu"));
        } else if (code != 0) {
            // Cookies illisibles : désactivés, le prochain essai automatique s'en passera.
            dropBrokenCookies(err);
            done({}, ytdlpError(err));
        } else if (result->path.isEmpty() || !QFileInfo::exists(result->path)) {
            done({}, QStringLiteral("fichier téléchargé introuvable"));
        } else {
            done(*result, {});
        }
    });
    connect(p, &QProcess::errorOccurred, this, [=](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *finished)
            return;
        *finished = true;
        p->deleteLater();
        done({}, QStringLiteral("impossible de lancer yt-dlp"));
    });
    p->start(m_bin, args);
    timer->start(15 * 60 * 1000);
    return p;
}

void YtDlp::browserCookie(const QString &browser, const QString &domain, const QString &name,
                          std::function<void(QString, QString)> cb)
{
    if (m_bin.isEmpty()) {
        cb({}, QStringLiteral("yt-dlp introuvable : installez-le ou utilisez « Mettre à jour yt-dlp » dans les paramètres"));
        return;
    }
    if (browser.isEmpty()) {
        cb({}, QStringLiteral("choisissez un navigateur"));
        return;
    }
    // Dossier temporaire privé (0700) : le fichier contient tous les cookies du navigateur.
    auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) {
        cb({}, QStringLiteral("dossier temporaire impossible à créer"));
        return;
    }
    const QString file = dir->filePath("cookies.txt");
    auto *p = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    // Sans URL, yt-dlp charge les cookies, les écrit dans --cookies, puis s'arrête (code 2).
    connect(p, &QProcess::finished, this, [p, dir, file, browser, domain, name, cb, finished](int, QProcess::ExitStatus st) {
        if (*finished)
            return;
        *finished = true;
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        QString text;
        if (QFile f(file); f.open(QIODevice::ReadOnly))
            text = QString::fromUtf8(f.readAll());
        QFile::remove(file);
        dir->remove();
        const QString value = netscapeCookie(text, domain, name);
        if (!value.isEmpty())
            cb(value, {});
        else if (st != QProcess::NormalExit)
            cb({}, QStringLiteral("lecture des cookies de %1 trop longue (trousseau verrouillé ?)").arg(browser));
        else if (text.isEmpty())
            cb({}, QStringLiteral("cookies de %1 illisibles%2").arg(browser,
                       err.contains(QLatin1String("ERROR")) ? " : " + ytdlpError(err) : QString()));
        else
            cb({}, QStringLiteral("aucune session %1 dans %2 : connectez-vous sur le site dans ce navigateur, puis réessayez")
                       .arg(domain, browser));
    });
    connect(p, &QProcess::errorOccurred, this, [p, dir, cb, finished](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *finished)
            return;
        *finished = true;
        p->deleteLater();
        dir->remove();
        cb({}, QStringLiteral("impossible de lancer yt-dlp"));
    });
    p->start(m_bin, {"--ignore-config", "--no-warnings", "--cookies-from-browser", browser, "--cookies", file});
    timer->start(90000);
}

void YtDlp::updateManaged(std::function<void(QString, bool)> cb)
{
    const QString target = m_managedDir + "/" + kManagedName;
    if (QFileInfo(target).isExecutable()) {
        // Copie déjà installée : mise à jour par yt-dlp lui-même.
        auto *p = new QProcess(this);
        auto onDone = [this, p, cb](int code) {
            const QString out = QString::fromUtf8(p->readAllStandardOutput() + p->readAllStandardError()).trimmed();
            p->deleteLater();
            const QString msg = out.isEmpty() ? QStringLiteral("impossible de lancer yt-dlp") : out.split('\n').last();
            selectNewest([cb, msg, code](QString) { cb(msg, code == 0); });
        };
        connect(p, &QProcess::finished, this, onDone);
        connect(p, &QProcess::errorOccurred, this, [onDone](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                onDone(-1);
        });
        p->start(target, {"-U"});
        return;
    }
    // Première installation : binaire officiel (curl est fourni avec Linux, macOS et Windows 10+).
    auto *p = new QProcess(this);
    auto onDone = [this, p, cb, target](int code) {
        p->deleteLater();
        if (code != 0 || !QFileInfo::exists(target)) {
            QFile::remove(target);
            selectNewest([cb](QString) { cb(QStringLiteral("téléchargement de yt-dlp impossible (curl est-il installé ?)"), false); });
            return;
        }
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
        selectNewest([cb](QString v) { cb(QStringLiteral("yt-dlp %1 installé").arg(v), true); });
    };
    connect(p, &QProcess::finished, this, onDone);
    // curl absent : finished n'est jamais émis, le rappel doit quand même arriver.
    connect(p, &QProcess::errorOccurred, this, [onDone](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            onDone(-1);
    });
    p->start("curl", {"-fsSL", "--max-time", "120", "-o", target,
                      kManagedUrl});
}
