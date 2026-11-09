#include "Player.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRandomGenerator>

#include "AudioEngine.h"

namespace {

// Délai avant de basculer en streaming si le téléchargement prioritaire traîne.
constexpr int kStreamFallbackMs = 12000;

}  // namespace

Player::Player(Database *db, Downloader *dl, YtDlp *yt, QObject *parent)
    : QObject(parent), m_db(db), m_dl(dl), m_yt(yt)
{
    m_volume = m_db->setting("volume", "80").toDouble();
    m_engine = AudioEngine::create(m_db, m_volume, this);
    connect(m_engine, &AudioEngine::pausedChanged, this, [this](bool paused) {
        m_paused = paused;
        emit stateChanged();
    });
    connect(m_engine, &AudioEngine::durationChanged, this, [this](double d) {
        m_duration = d;
        emit stateChanged();
    });
    connect(m_engine, &AudioEngine::volumeChanged, this, [this](double v) {
        m_volume = v;
        emit stateChanged();
    });
    connect(m_engine, &AudioEngine::loaded, this, [this] {
        qInfo() << "lecture démarrée (" << m_source << ")";
        m_loading = false;
        m_consecutiveErrors = 0;
        m_status.clear();
        emit stateChanged();
        prefetchNext();
    });
    connect(m_engine, &AudioEngine::restarted, this, &Player::seeked);
    connect(m_engine, &AudioEngine::finished, this, [this](bool error, const QString &message) {
        if (error)
            onPlaybackError(message);
        else
            advance(true);
    });

    connect(m_dl, &Downloader::finished, this, &Player::onDownloadFinished);
    m_streamFallback.setSingleShot(true);
    connect(&m_streamFallback, &QTimer::timeout, this, [this] {
        if (m_waitingDownload > 0) {
            qInfo() << "téléchargement lent, bascule en streaming";
            startStreaming(m_generation);
        }
    });
    // Le curseur de volume envoie des dizaines de valeurs par seconde : seule la
    // dernière est écrite en base (et à la fermeture si besoin).
    m_volumeSave.setSingleShot(true);
    m_volumeSave.setInterval(600);
    auto saveVolume = [this] {
        m_volumeSave.stop();
        m_db->setSetting("volume", QString::number(m_volume));
    };
    connect(&m_volumeSave, &QTimer::timeout, this, saveVolume);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this, saveVolume] {
        if (m_volumeSave.isActive())
            saveVolume();
    });
    m_listenTimer.setInterval(1000);
    connect(&m_listenTimer, &QTimer::timeout, this, &Player::trackListening);
    m_listenTimer.start();
}

void Player::trackListening()
{
    const auto t = current();
    if (!t || m_source.isEmpty() || m_loading || t->id != m_listenTrack) {
        m_lastPos = -1;
        return;
    }
    const double pos = position();
    if (m_lastPos >= 0) {
        // Seule la lecture normale compte : un saut (avant ou arrière) est ignoré.
        const double delta = pos - m_lastPos;
        if (delta > 0 && delta < 3)
            m_listened += delta;
    }
    m_lastPos = pos;
    const double dur = m_duration > 0 ? m_duration : t->duration;
    if (!m_playRow && dur > 0 && m_listened >= dur * 0.5) {
        m_playRow = m_db->recordPlay(t->id, m_listened);
        m_savedListened = m_listened;
        qInfo() << "écoute comptée :" << t->title;
        emit playCounted(t->id);
    } else if (m_playRow && m_listened - m_savedListened >= 10) {
        m_db->updatePlay(m_playRow, m_listened);
        m_savedListened = m_listened;
    }
}

void Player::finishListening()
{
    // Temps d'écoute final du morceau précédent (au-delà des 50 % déjà enregistrés).
    if (m_playRow && m_listened > m_savedListened)
        m_db->updatePlay(m_playRow, m_listened);
    m_playRow = 0;
    m_listened = 0;
    m_savedListened = 0;
    m_lastPos = -1;
    m_listenTrack = 0;
}

Player::~Player()
{
    delete m_engine;  // avant les membres qu'il pourrait encore signaler
}

std::optional<Track> Player::current() const
{
    if (m_index >= 0 && m_index < m_queue.size())
        return m_queue[m_index];
    return std::nullopt;
}

double Player::position() const
{
    return m_source.isEmpty() ? 0 : m_engine->position();
}

void Player::setStatus(const QString &s)
{
    m_status = s;
    emit stateChanged();
}

void Player::bumpQueue()
{
    ++m_queueVersion;
    emit queueChanged();
}

// ---------- Chargement ----------

void Player::load(int index, bool resetRetry)
{
    if (index < 0 || index >= m_queue.size())
        return;
    finishListening();
    m_index = index;
    ++m_generation;
    m_waitingDownload = 0;
    m_streamFallback.stop();
    m_loading = true;
    if (resetRetry)
        m_retried = false;
    // Toujours relire la base : le morceau a pu être téléchargé entre-temps.
    if (auto fresh = m_db->track(m_queue[index].id))
        m_queue[index] = *fresh;
    const Track &t = m_queue[index];
    m_duration = t.duration;
    emit trackChanged();

    qInfo() << "chargement :" << t.title << (t.isLocal() ? "(local)" : "(à télécharger)");
    if (t.isLocal()) {
        loadFile(t.filePath, "local");
        return;
    }
    // Flux préparé pendant le morceau précédent (résultat de recherche) : départ immédiat.
    // Écouté pour de bon, il rejoint le cache en arrière-plan comme les autres.
    if (m_pre.trackId == t.id && m_pre.at.secsTo(QDateTime::currentDateTimeUtc()) < 2 * 3600) {
        const PreStream pre = std::exchange(m_pre, {});
        qInfo() << "lecture immédiate du flux préparé";
        loadFile(pre.url, "stream", pre.userAgent);
        if (m_db->settingBool("auto_cache", true))
            m_dl->enqueue(t.id);
        return;
    }
    // Arrête le morceau précédent pendant le chargement.
    m_engine->stop();
    m_source.clear();
    if (m_db->settingBool("auto_cache", true)) {
        // Un seul transfert : on télécharge en priorité puis on lit le fichier.
        m_waitingDownload = t.id;
        setStatus(QStringLiteral("Téléchargement…"));
        m_dl->prioritize(t.id);
        m_streamFallback.start(kStreamFallbackMs);
    } else {
        startStreaming(m_generation);
    }
}

void Player::onDownloadFinished(qint64 trackId, bool ok, const QString &error)
{
    // Met à jour les copies du morceau dans la file.
    if (auto t = m_db->track(trackId))
        refreshTrack(*t);
    if (trackId != m_waitingDownload)
        return;
    m_waitingDownload = 0;
    m_streamFallback.stop();
    const auto t = current();
    if (ok && t && t->isLocal()) {
        loadFile(t->filePath, "local");
    } else {
        qInfo() << "téléchargement impossible, streaming :" << error;
        startStreaming(m_generation);
    }
}

void Player::startStreaming(quint64 generation)
{
    const auto t = current();
    if (!t)
        return;
    m_waitingDownload = 0;
    setStatus(QStringLiteral("Connexion à YouTube…"));
    m_yt->resolveStream(t->ytId, [this, generation](QString url, QString ua, QString err) {
        if (generation != m_generation)
            return;  // l'utilisateur est passé à autre chose
        if (!err.isEmpty()) {
            onPlaybackError(err);
            return;
        }
        loadFile(url, "stream", ua);
    });
}

void Player::loadFile(const QString &target, const QString &source, const QString &userAgent)
{
    const auto t = current();
    if (!t)
        return;
    const QString title = t->artist.isEmpty() ? t->title : t->artist + " — " + t->title;
    // En pilotage externe, le départ est donné à l'heure convenue par le Jam.
    if (!m_engine->load(target, title, userAgent, m_external)) {
        onPlaybackError(QStringLiteral("le lecteur a refusé le fichier"));
        return;
    }
    m_source = source;
    m_status.clear();
    m_paused = m_external;
    m_db->markStarted(t->id);
    m_listenTrack = t->id;
    emit stateChanged();
}

void Player::onPlaybackError(const QString &msg)
{
    m_loading = false;
    qWarning() << "erreur de lecture :" << msg;
    const auto t = current();
    // Fichier local corrompu, ou URL de flux expirée : une seule nouvelle tentative.
    if (!m_retried && t) {
        m_retried = true;
        if (m_source == "local") {
            m_dl->removeFile(t->id);
            if (auto fresh = m_db->track(t->id))
                m_queue[m_index] = *fresh;
            startStreaming(m_generation);
        } else {
            load(m_index, false);
        }
        return;
    }
    m_source.clear();
    setStatus({});
    emit errorOccurred(t ? QStringLiteral("« %1 » : %2").arg(t->title, msg) : msg);
    if (++m_consecutiveErrors < 5)
        QTimer::singleShot(800, this, [this] { advance(false); });
}

void Player::advance(bool naturalEnd)
{
    if (m_index < 0)
        return;
    if (m_external) {
        const qint64 id = current() ? current()->id : 0;
        finishListening();
        m_source.clear();
        m_loading = false;
        emit stateChanged();
        if (naturalEnd)
            emit endReached(id);
        return;
    }
    int next = -1;
    if (naturalEnd && m_repeat == RepeatOne)
        next = m_index;
    else if (m_index + 1 < m_queue.size())
        next = m_index + 1;
    else if (m_repeat != RepeatOff && !m_queue.isEmpty())
        next = 0;
    if (next >= 0) {
        load(next, true);
    } else {
        finishListening();
        m_source.clear();
        m_loading = false;
        emit stateChanged();
    }
}

void Player::prefetchNext()
{
    if (!m_db->settingBool("prefetch", true) || m_index + 1 >= m_queue.size())
        return;
    const Track &next = m_queue[m_index + 1];
    if (next.isLocal())
        return;
    // Résultat de recherche : on prépare seulement le flux (rien n'est téléchargé).
    if (m_browseOnly.contains(next.id)) {
        preResolve(next);
        return;
    }
    // Pas de préchargement des vidéos très longues (compilations de plusieurs heures).
    if (m_db->settingBool("auto_cache", true) && next.duration < 20 * 60)
        m_dl->enqueue(next.id);
}

void Player::preResolve(const Track &t)
{
    if (t.ytId.isEmpty() || m_preResolving == t.id || m_pre.trackId == t.id)
        return;
    m_preResolving = t.id;
    m_yt->resolveStream(t.ytId, [this, id = t.id](QString url, QString ua, QString err) {
        if (m_preResolving == id)
            m_preResolving = 0;
        if (!err.isEmpty() || url.isEmpty())
            return;  // le chargement normal prendra le relais
        m_pre = {id, url, ua, QDateTime::currentDateTimeUtc()};
        qInfo() << "flux du morceau suivant prêt (résultat de recherche, non téléchargé)";
    });
}

// ---------- Commandes ----------

void Player::playList(const QList<Track> &tracks, int start, bool shuffleOn, bool browseRest)
{
    if (tracks.isEmpty())
        return;
    m_browseOnly.clear();
    if (browseRest)
        for (int i = 0; i < tracks.size(); ++i)
            if (i != start)
                m_browseOnly.insert(tracks[i].id);
    m_queue = tracks;
    m_unshuffled.clear();
    m_index = qBound(0, start, int(tracks.size()) - 1);
    m_shuffle = shuffleOn;
    if (shuffleOn)
        shuffleQueue();
    bumpQueue();
    emit stateChanged();
    load(m_index, true);
}

void Player::jump(int index)
{
    if (index >= 0 && index < m_queue.size())
        load(index, true);
}

void Player::next()
{
    advance(false);
}

void Player::prev()
{
    if (m_index < 0)
        return;
    if (position() < 3 && m_index > 0)
        load(m_index - 1, true);
    else if (m_source.isEmpty())
        load(m_index, true);
    else
        seek(0);
}

void Player::toggle()
{
    if (m_source.isEmpty()) {
        // File remplie par « Ajouter à la file » sans morceau en cours : on part du premier.
        if (!m_loading)
            load(qMax(0, m_index), true);
        return;
    }
    m_engine->togglePause();
}

void Player::seek(double pos)
{
    if (m_source.isEmpty())
        return;
    m_engine->seek(qMax(0.0, pos));
}

void Player::setVolume(double v)
{
    m_volume = qBound(0.0, v, 130.0);
    m_engine->setVolume(m_volume);
    m_volumeSave.start();
    emit stateChanged();
}

void Player::setShuffle(bool on)
{
    if (on != m_shuffle) {
        m_shuffle = on;
        if (on)
            shuffleQueue();
        else
            unshuffleQueue();
        bumpQueue();
    }
    emit stateChanged();
}

void Player::shuffleQueue()
{
    // Toute la file est mélangée : le morceau en cours passe en tête, tous les
    // autres (y compris ceux d'avant) sont tirés au hasard derrière lui.
    m_unshuffled = m_queue;
    if (m_queue.isEmpty())
        return;
    int start = 0;
    if (m_index >= 0 && m_index < m_queue.size()) {
        m_queue.move(m_index, 0);
        m_index = 0;
        start = 1;
    }
    auto *rng = QRandomGenerator::global();
    for (int i = int(m_queue.size()) - 1; i > start; --i)
        m_queue.swapItemsAt(i, start + int(rng->bounded(i - start + 1)));
}

void Player::unshuffleQueue()
{
    // Retour à l'ordre d'origine, en reprenant à la place du morceau en cours.
    if (m_unshuffled.isEmpty())
        return;
    const qint64 cur = current() ? current()->id : 0;
    m_queue = std::exchange(m_unshuffled, {});
    m_index = m_queue.isEmpty() ? -1 : qMin(m_index, int(m_queue.size()) - 1);
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue[i].id == cur) {
            m_index = i;
            break;
        }
}

void Player::setRepeat(int r)
{
    m_repeat = qBound(0, r, 2);
    emit stateChanged();
}

void Player::enqueue(const Track &t, bool playNext)
{
    const int pos = (playNext && m_index >= 0) ? m_index + 1 : int(m_queue.size());
    m_queue.insert(pos, t);
    if (m_shuffle) {
        // Dans l'ordre d'origine : juste après le morceau en cours (« lire ensuite ») ou à la fin.
        int at = int(m_unshuffled.size());
        if (playNext && current())
            for (int i = 0; i < m_unshuffled.size(); ++i)
                if (m_unshuffled[i].id == current()->id) {
                    at = i + 1;
                    break;
                }
        m_unshuffled.insert(at, t);
    }
    // Ajouté exprès par l'utilisateur : téléchargé d'avance comme une playlist.
    m_browseOnly.remove(t.id);
    if (m_db->settingBool("prefetch", true) && m_db->settingBool("auto_cache", true))
        m_dl->enqueue(t.id);
    bumpQueue();
}

void Player::removeAt(int index)
{
    if (index < 0 || index >= m_queue.size() || index == m_index)
        return;
    const Track removed = m_queue.takeAt(index);
    for (int i = 0; i < m_unshuffled.size(); ++i)
        if (m_unshuffled[i].id == removed.id) {
            m_unshuffled.removeAt(i);
            break;
        }
    if (index < m_index)
        --m_index;
    bumpQueue();
}

void Player::moveItem(int from, int to)
{
    const int n = int(m_queue.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to)
        return;
    m_queue.move(from, to);
    if (m_index == from)
        m_index = to;
    else if (from < m_index && to >= m_index)
        --m_index;
    else if (from > m_index && to <= m_index)
        ++m_index;
    bumpQueue();
}

void Player::clearQueue()
{
    const auto cur = current();
    m_queue.clear();
    if (cur)
        m_queue.append(*cur);
    m_unshuffled = m_shuffle ? m_queue : QList<Track>();
    m_index = m_queue.isEmpty() ? -1 : 0;
    bumpQueue();
}

void Player::setExternalControl(bool on)
{
    m_external = on;
    if (on) {
        m_shuffle = false;
        m_unshuffled.clear();
        m_repeat = RepeatOff;
    }
    setSpeed(1.0);
    emit stateChanged();
}

void Player::setPaused(bool paused)
{
    if (m_source.isEmpty() || paused == m_paused)
        return;
    m_engine->setPaused(paused);
    m_paused = paused;
    emit stateChanged();
}

void Player::setSpeed(double speed)
{
    speed = qBound(0.9, speed, 1.1);
    if (qAbs(speed - m_speed) < 0.001)
        return;
    m_speed = speed;
    m_engine->setSpeed(m_speed);
}

void Player::stop()
{
    finishListening();
    ++m_generation;
    m_waitingDownload = 0;
    m_streamFallback.stop();
    m_engine->stop();
    m_source.clear();
    m_loading = false;
    m_status.clear();
    emit stateChanged();
}

void Player::syncQueue(const QList<Track> &tracks, int index)
{
    const auto cur = current();
    const bool valid = index >= 0 && index < tracks.size();
    const bool same = valid && cur && cur->id == tracks[index].id && (!m_source.isEmpty() || m_loading);
    m_browseOnly.clear();
    m_queue = tracks;
    m_unshuffled.clear();
    m_index = valid ? index : -1;
    bumpQueue();
    if (!valid)
        stop();
    else if (!same)
        load(index, true);
    else
        emit trackChanged();
}

void Player::refreshTrack(const Track &t)
{
    bool changed = false;
    for (Track &q : m_queue)
        if (q.id == t.id) {
            q = t;
            changed = true;
        }
    if (changed)
        bumpQueue();
}
