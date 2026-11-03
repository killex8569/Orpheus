#include "Downloader.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "Matcher.h"

static constexpr int kMaxAttempts = 3;
static constexpr int kBackgroundGapMs = 1500;

Downloader::Downloader(Database *db, YtDlp *yt, const Paths &paths, QObject *parent)
    : QObject(parent), m_db(db), m_yt(yt), m_paths(paths)
{
    // Rangement différé : regroupe les changements rapprochés en une seule passe.
    m_placeTimer.setSingleShot(true);
    m_placeTimer.setInterval(1500);
    connect(&m_placeTimer, &QTimer::timeout, this, [this] {
        m_placing = true;
        m_placeAgain = false;
        m_placedMoved = m_placedFailed = 0;
        m_placeQueue = m_db->tracksToPlace(m_paths.library);
        placeNext();
    });
}

void Downloader::resumePending()
{
    // « downloading » : téléchargement coupé par la fermeture de l'application.
    for (const QString &state : {QStringLiteral("downloading"), QStringLiteral("queued")})
        for (const Track &t : m_db->tracksInState(state))
            if (!m_queue.contains(t.id) && !m_active.contains(t.id)) {
                m_db->setState(t.id, "queued");
                m_queue.append(t.id);
            }
    pump();
}

void Downloader::enqueue(qint64 trackId)
{
    const auto t = m_db->track(trackId);
    if (!t || t->isLocal() || m_active.contains(trackId) || m_queue.contains(trackId))
        return;
    m_db->setState(trackId, "queued");
    m_queue.append(trackId);
    emit statusChanged();
    pump();
}

void Downloader::prioritize(qint64 trackId)
{
    if (auto it = m_active.find(trackId); it != m_active.end()) {
        it->priority = true;
        return;
    }
    m_queue.removeAll(trackId);
    // Un seul transfert à la fois : l'arrière-plan s'efface (il reprendra ensuite).
    for (auto it = m_active.begin(); it != m_active.end(); ++it)
        if (!it->priority && it->process) {
            it->preempted = true;
            it->process->kill();
        }
    start(trackId, true);
}

double Downloader::progressOf(qint64 trackId) const
{
    const auto it = m_active.constFind(trackId);
    return it == m_active.cend() ? -1 : it->progress;
}

QString Downloader::activeTitle() const
{
    QStringList parts;
    for (const Active &a : m_active)
        parts << QStringLiteral("%1 — %2 %").arg(a.title).arg(qRound(a.progress));
    return parts.join('\n');
}

bool Downloader::canStartBackground() const
{
    // Rien en arrière-plan pendant un téléchargement prioritaire.
    int background = 0;
    for (const Active &a : m_active) {
        if (a.priority)
            return false;
        ++background;
    }
    // Chaque morceau passe surtout du temps à attendre YouTube (extraction, délai
    // imposé de quelques secondes) : deux en parallèle doublent le débit.
    const int max = qBound(1, m_db->setting("parallel_downloads", "2").toInt(), 4);
    return background < max;
}

void Downloader::pump()
{
    if (m_pumpScheduled || m_queue.isEmpty() || !canStartBackground())
        return;
    m_pumpScheduled = true;
    QTimer::singleShot(kBackgroundGapMs, this, [this] {
        m_pumpScheduled = false;
        if (m_queue.isEmpty() || !canStartBackground())
            return;
        start(m_queue.takeFirst(), false);
        pump();  // remplit l'emplacement suivant, s'il y en a un
    });
}

void Downloader::start(qint64 id, bool priority)
{
    const int attempt = m_attempts.take(id) + 1;
    const auto t = m_db->track(id);
    if (!t) {
        pump();
        return;
    }
    if (t->isLocal()) {
        emit finished(id, true, {});
        pump();
        return;
    }
    m_db->setState(id, "downloading");
    Active a;
    a.priority = priority;
    a.attempt = attempt;
    a.title = t->title;
    m_active.insert(id, a);
    emit statusChanged();
    a.process = m_yt->download(
        t->ytId, m_paths.library, m_paths.covers,
        [this, id](double pct) {
            if (auto it = m_active.find(id); it != m_active.end()) {
                it->progress = pct;
                emit progress(id, pct);
            }
        },
        [this, id](YtDlp::DownloadResult r, QString err) { onDone(id, r, err); });
    m_active[id].process = a.process;
}

void Downloader::onDone(qint64 id, const YtDlp::DownloadResult &r, const QString &err)
{
    const Active a = m_active.take(id);
    const auto t = m_db->track(id);
    if (a.preempted && t) {
        // Interrompu volontairement : reprend en tête de file, sans compter d'échec.
        cleanupPartial(t->ytId);
        m_db->setState(id, "queued");
        m_attempts[id] = a.attempt - 1;
        m_queue.prepend(id);
        emit statusChanged();
        pump();
        return;
    }
    if (err.isEmpty() && t) {
        m_db->setFile(id, QDir::cleanPath(r.path), r.codec, r.bitrate, QFileInfo(r.path).size(), r.duration);
        QString title = r.title, artist = r.artist;
        if (title.isEmpty() && t->title == t->ytId && !r.rawTitle.isEmpty()) {
            // Lien collé : seul l'identifiant de la vidéo était connu.
            const auto split = Matcher::splitYoutubeTitle(r.rawTitle, r.channel);
            artist = split.first;
            title = split.second.isEmpty() ? r.rawTitle : split.second;
        }
        m_db->enrichMetadata(id, title, artist, r.album);
        // Téléchargé dans la bibliothèque, puis rangé dans le dossier de sa playlist s'il y a lieu.
        placeFile(id);
        qInfo() << "en cache :" << t->title << r.codec << r.bitrate << "kb/s";
        emit finished(id, true, {});
        enforceCacheLimit();
    } else if (t) {
        cleanupPartial(t->ytId);
        const QString low = err.toLower();
        const bool permanent = low.contains("unavailable") || low.contains("private") || low.contains("removed")
                               || low.contains("copyright") || low.contains("not available") || low.contains("introuvable");
        qWarning() << "téléchargement échoué :" << t->title << err;
        if (a.priority) {
            // L'écoute bascule en streaming ; le cache sera retenté plus tard en arrière-plan.
            m_db->setState(id, permanent ? "failed" : "queued", err);
            if (!permanent && !m_queue.contains(id))
                m_queue.append(id);
        } else if (!permanent && a.attempt < kMaxAttempts) {
            m_db->setState(id, "queued", err);
            m_attempts[id] = a.attempt;
            // Retour dans la file (et non lancement direct) : on garde un seul
            // téléchargement à la fois, ce qui évite d'aggraver les refus de YouTube.
            const int delay = 10000 * a.attempt * a.attempt;  // 10 s, 40 s
            QTimer::singleShot(delay, this, [this, id] {
                if (!m_active.contains(id) && !m_queue.contains(id)) {
                    m_queue.append(id);
                    pump();
                }
            });
        } else {
            m_db->setState(id, "failed", err);
        }
        emit finished(id, false, err);
    }
    emit statusChanged();
    pump();
}

void Downloader::cleanupPartial(const QString &ytId)
{
    // Restes d'un téléchargement interrompu (.part, .webm, miniature non déplacée…).
    const QDir lib(m_paths.library);
    for (const QString &f : lib.entryList({ytId + ".*"}, QDir::Files))
        QFile::remove(lib.filePath(f));
}

QString cleanFileName(const QString &name, int maxLength)
{
    QString clean;
    for (const QChar c : name)
        clean += (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) ? QChar(' ') : c;
    clean = clean.simplified().left(maxLength);
    while (clean.endsWith('.') || clean.endsWith(' '))
        clean.chop(1);
    return clean;
}

QString Downloader::fileNameFor(const Track &t, const QString &ext, bool inLibrary)
{
    const QString suffix = ext.isEmpty() ? QString() : "." + ext;
    if (inLibrary)
        return t.ytId + suffix;
    // Nom lisible, valable sous Windows, macOS et Linux ; l'id évite les collisions.
    const QString clean = cleanFileName(t.artist.isEmpty() ? t.title : t.artist + " - " + t.title);
    return (clean.isEmpty() ? QString() : clean + " ") + "[" + t.ytId + "]" + suffix;
}

bool Downloader::placeFile(qint64 trackId)
{
    const auto t = m_db->track(trackId);
    if (!t || !t->isLocal())
        return true;
    const QString folder = m_db->folderForTrack(trackId);
    const bool inLibrary = folder.isEmpty();
    const QString dir = QDir::cleanPath(inLibrary ? m_paths.library : folder);
    const QString src = QDir::cleanPath(t->filePath);
    const QString dst = dir + "/" + fileNameFor(*t, QFileInfo(src).suffix(), inLibrary);
    if (src == dst) {
        if (t->filePath != dst)
            m_db->setFilePath(trackId, dst);  // même fichier, chemin normalisé (séparateurs Windows)
        return true;
    }
    if (!QDir().mkpath(dir)) {
        qWarning() << "rangement impossible, dossier inaccessible :" << dir;
        return false;
    }
    if (QFileInfo::exists(dst)) {
        // Copie identique laissée par un rangement interrompu : on la reprend.
        if (QFileInfo(dst).size() == QFileInfo(src).size() && QFile::remove(src)) {
            m_db->setFilePath(trackId, dst);
            return true;
        }
        qWarning() << "rangement impossible, un autre fichier porte déjà ce nom :" << dst;
        return false;
    }
    if (!QFile::rename(src, dst)) {
        // Autre disque : copie puis suppression. Fichier en cours de lecture (Windows) : on
        // annule et on réessaiera plus tard.
        if (!QFile::copy(src, dst))
            return false;
        if (!QFile::remove(src)) {
            QFile::remove(dst);
            return false;
        }
    }
    m_db->setFilePath(trackId, dst);
    qInfo() << "rangé :" << t->title << "→" << dir;
    return true;
}

void Downloader::schedulePlacement()
{
    if (m_placing) {
        m_placeAgain = true;
        return;
    }
    if (!m_placeTimer.isActive())
        m_placeTimer.start();
}

void Downloader::placeNext()
{
    if (m_placeQueue.isEmpty()) {
        m_placing = false;
        if (m_placedMoved || m_placedFailed)
            emit placementDone(m_placedMoved, m_placedFailed);
        if (m_placeAgain)
            schedulePlacement();
        return;
    }
    const qint64 id = m_placeQueue.takeFirst();
    // Un morceau en cours de téléchargement sera rangé à la fin de celui-ci.
    if (!m_active.contains(id)) {
        const auto before = m_db->track(id);
        if (!placeFile(id))
            ++m_placedFailed;
        else if (const auto after = m_db->track(id); before && after && QDir::cleanPath(before->filePath) != after->filePath)
            ++m_placedMoved;
    }
    QTimer::singleShot(0, this, &Downloader::placeNext);
}

void Downloader::removeFile(qint64 trackId)
{
    if (const auto t = m_db->track(trackId)) {
        if (!t->filePath.isEmpty())
            QFile::remove(t->filePath);
        qInfo() << "retiré du cache :" << t->title;
    }
    m_db->clearFile(trackId);
}

void Downloader::enforceCacheLimit()
{
    const qint64 limitMb = m_db->setting("cache_max_mb", "20480").toLongLong();
    if (limitMb <= 0)
        return;
    const qint64 limit = limitMb * 1024 * 1024;
    qint64 total = m_db->cacheSize().first;
    if (total <= limit)
        return;
    for (const Track &t : m_db->evictionCandidates()) {
        if (total <= limit)
            break;
        // On ne touche pas à ce qui vient d'être écouté.
        if (t.lastPlayed > 0 && nowSecs() - t.lastPlayed < 3600)
            continue;
        removeFile(t.id);
        total -= t.fileSize;
    }
}
