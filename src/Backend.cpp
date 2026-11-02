#include "Backend.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QLocale>
#include <QRandomGenerator>
#include <QUrl>
#include <algorithm>

#include "Matcher.h"

Backend::Backend(const Paths &paths, QObject *parent)
    : QObject(parent), m_paths(paths), m_search(&m_paths), m_local(&m_paths), m_playlistTracks(&m_paths), m_queue(&m_paths),
      m_statsTracks(&m_paths)
{
}

Backend::~Backend() = default;

bool Backend::init(QString *error)
{
    if (!m_db.open(m_paths.db, error))
        return false;
    m_yt = new YtDlp(m_paths.bin, this);
    m_yt->setCookiesBrowser(m_db.setting("yt_cookies_browser"));
    connect(m_yt, &YtDlp::cookiesUnavailable, this, [this](const QString &browser) {
        QString msg = QStringLiteral("Cookies de %1 illisibles : Orpheus continue sans eux.").arg(browser);
#ifdef Q_OS_WIN
        // Chrome/Edge/Brave verrouillent et chiffrent leurs cookies sous Windows.
        if (browser != QLatin1String("firefox"))
            msg += QStringLiteral(" Sous Windows, seul Firefox fonctionne (Paramètres → YouTube).");
#else
        msg += QStringLiteral(" Fermez le navigateur ou choisissez-en un autre dans les paramètres.");
#endif
        emit toast(msg, true);
    });
    m_dl = new Downloader(&m_db, m_yt, m_paths, this);
    m_spotify = new Spotify(&m_db, this);
    m_player = new Player(&m_db, m_dl, m_yt, this);
    m_resolver = std::make_unique<TrackResolver>(&m_db, m_yt);
    m_importer = new Importer(&m_db, m_yt, m_spotify, m_dl, m_resolver.get(), this);
    m_lyrics = new LyricsService(&m_db, this);
    m_jam = new JamController(&m_db, m_player, &m_paths, this);
    connect(m_jam, &JamController::toast, this, &Backend::toast);
    connect(m_jam, &JamController::queueChanged, this, &Backend::updateMarks);
    m_updates = new UpdateChecker(this);
    connect(m_updates, &UpdateChecker::updateFound, this, [this](const QString &v) {
        emit toast(QStringLiteral("Mise à jour disponible : Orpheus %1 (onglet Mises à jour)").arg(v), false);
    });
    m_searchSource = m_db.setting("search_source", "youtube");

    connect(m_player, &Player::stateChanged, this, &Backend::playerChanged);
    connect(m_player, &Player::trackChanged, this, &Backend::trackChanged);
    connect(m_player, &Player::trackChanged, this, &Backend::playerChanged);
    connect(m_player, &Player::queueChanged, this, [this] {
        m_queue.setTracks(m_player->queue());
        emit trackChanged();
    });
    connect(m_player, &Player::trackChanged, this, [this] { m_lyrics->setCurrent(m_player->current(), m_player->duration()); });
    connect(m_lyrics, &LyricsService::changed, this, &Backend::lyricsChanged);
    connect(m_importer, &Importer::jobsChanged, this, &Backend::refreshJobs);
    connect(m_importer, &Importer::progress, this, &Backend::scheduleRefresh);
    connect(m_importer, &Importer::playlistsChanged, this, &Backend::refreshPlaylists);
    connect(m_importer, &Importer::toast, this, &Backend::toast);
    connect(m_importer, &Importer::finished, this, [this](qint64 playlistId) {
        refreshPlaylists();
        if (m_currentPlaylistId == playlistId)
            openPlaylist(playlistId);
    });
    connect(m_player, &Player::seeked, this, &Backend::seeked);
    connect(m_player, &Player::playCounted, this, [this] { loadStats(m_statsDays); });
    connect(m_player, &Player::errorOccurred, this, [this](const QString &m) { emit toast(m, true); });
    connect(m_dl, &Downloader::statusChanged, this, &Backend::downloadsChanged);
    connect(m_dl, &Downloader::progress, this, &Backend::downloadsChanged);
    connect(m_dl, &Downloader::finished, this, [this](qint64 id, bool ok, const QString &) {
        // Morceau gardé en local : ses paroles aussi, pour l'écoute hors ligne.
        if (ok)
            m_lyrics->enqueue(id);
        if (const auto t = m_db.track(id)) {
            for (TrackModel *m : {&m_search, &m_local, &m_playlistTracks, m_jam->queue()})
                m->updateTrack(*t);
        }
        scheduleRefresh();
    });

    // Position du lecteur rafraîchie 4 fois par seconde pendant la lecture.
    m_positionTimer.setInterval(250);
    connect(&m_positionTimer, &QTimer::timeout, this, &Backend::positionChanged);
    m_positionTimer.start();
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(700);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        refreshPlaylists();
        refreshJobs();
    });

    m_targetPlaylistId = m_db.setting("target_playlist").toLongLong();
    refreshPlaylists();
    refreshJobs();
    m_yt->selectNewest([this](QString v) {
        m_ytdlpVersion = v;
        emit accountsChanged();
        // Copie gérée : installée puis mise à jour silencieusement à chaque démarrage.
        // Les paquets du système ont souvent des semaines de retard, et YouTube
        // refuse alors les téléchargements (HTTP 403).
        m_yt->updateManaged([this](QString msg, bool ok) {
            if (!ok)
                qWarning() << "mise à jour de yt-dlp impossible :" << msg;
            m_yt->version([this](QString v2) {
                m_ytdlpVersion = v2;
                emit accountsChanged();
            });
        });
    });
    // Les téléchargements en attente reprennent quelques secondes après le démarrage.
    QTimer::singleShot(3000, m_dl, &Downloader::resumePending);
    // Paroles des morceaux déjà en local, récupérées tranquillement en arrière-plan.
    QTimer::singleShot(15000, m_lyrics, &LyricsService::enqueueMissing);
    // Une seule vérification des mises à jour au lancement (désactivable).
    if (m_db.settingBool("update_check", true))
        QTimer::singleShot(4000, m_updates, &UpdateChecker::check);
    loadStats(m_statsDays);
    return true;
}

// ---------- Propriétés ----------

QString Backend::cover() const
{
    const auto t = m_player->current();
    if (!t)
        return {};
    const QString local = m_paths.coverFile(t->ytId);
    if (QFileInfo::exists(local))
        return QUrl::fromLocalFile(local).toString();
    return t->thumbnail.startsWith("http") ? t->thumbnail : thumbUrl(t->ytId);
}

QString Backend::quality() const
{
    const auto t = m_player->current();
    return (t && m_player->source() == "local") ? t->quality() : QString();
}

double Backend::cacheProgress() const
{
    const auto t = m_player->current();
    return t ? m_dl->progressOf(t->id) : -1;
}

QString Backend::downloadsText() const
{
    const int n = downloadsCount();
    if (n == 0)
        return {};
    const QString active = m_dl->activeTitle();
    return QStringLiteral("%1 téléchargement(s)%2").arg(n).arg(active.isEmpty() ? QString() : "\n" + active);
}

QString Backend::ytdlpInfo() const
{
    if (m_yt->binary().isEmpty())
        return QStringLiteral("yt-dlp introuvable");
    return QStringLiteral("%1 (%2)%3")
        .arg(m_yt->binary(), m_ytdlpVersion, m_yt->isManaged() ? QStringLiteral(" — géré par Orpheus") : QString());
}

QString Backend::cacheInfo() const
{
    const auto [bytes, count] = m_db.cacheSize();
    return QStringLiteral("%1 (%2 morceaux)").arg(QLocale().formattedDataSize(bytes)).arg(count);
}

// ---------- Rafraîchissements ----------

void Backend::refreshPlaylists()
{
    m_playlists.clear();
    for (const Playlist &p : m_db.playlists())
        m_playlists.append(p.toMap());
    emit playlistsChanged();
    if (m_currentPlaylistId > 0) {
        if (const auto p = m_db.playlist(m_currentPlaylistId)) {
            m_currentPlaylist = p->toMap();
            emit currentPlaylistChanged();
        }
    }
    updateMarks();
    emit downloadsChanged();
}

QString Backend::targetPlaylistName() const
{
    const auto p = m_targetPlaylistId > 0 ? m_db.playlist(m_targetPlaylistId) : std::nullopt;
    return p && p->kind != "cache" ? p->name : QString();
}

void Backend::setTargetPlaylist(qint64 id)
{
    if (id == m_targetPlaylistId)
        return;
    m_targetPlaylistId = id;
    m_db.setSetting("target_playlist", QString::number(id));
}

void Backend::updateMarks()
{
    QSet<qint64> ids;
    QSet<QString> keys;
    // Pendant un Jam : morceaux de la file commune ; sinon, playlist en cours de remplissage.
    const bool jam = m_jam && m_jam->active();
    if (jam || !targetPlaylistName().isEmpty()) {
        for (const Track &t : jam ? m_jam->queue()->tracks() : m_db.playlistTracks(m_targetPlaylistId)) {
            ids.insert(t.id);
            if (const QString k = Matcher::matchKey(t.artist, t.title); !k.isEmpty())
                keys.insert(k);
        }
    }
    m_search.setMarks(ids, keys);
    m_local.setMarks(ids, keys);
}

QVariantMap Backend::playlistMarks(const QString &list, int row)
{
    QVariantMap out;
    TrackModel *m = model(list);
    const Track *t = m ? m->at(row) : nullptr;
    if (!t)
        return out;
    qint64 id = t->id;
    if (id <= 0 && !t->ytId.isEmpty())
        if (const auto known = m_db.trackByYt(t->ytId))
            id = known->id;
    const auto found = m_db.playlistsContaining(id, Matcher::matchKey(t->artist, t->title));
    for (auto it = found.cbegin(); it != found.cend(); ++it)
        out.insert(QString::number(it.key()), it.value() ? QStringLiteral("same") : QStringLiteral("similar"));
    return out;
}

void Backend::refreshJobs()
{
    m_jobs.clear();
    for (const ImportJob &j : m_db.jobs())
        m_jobs.append(j.toMap());
    emit jobsChanged();
}

void Backend::scheduleRefresh()
{
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

TrackModel *Backend::model(const QString &list)
{
    if (list == "search")
        return &m_search;
    if (list == "local")
        return &m_local;
    if (list == "playlist")
        return &m_playlistTracks;
    if (list == "queue")
        return &m_queue;
    if (list == "stats")
        return &m_statsTracks;
    if (list == "jam")
        return m_jam->queue();
    return nullptr;
}

// ---------- Recherche et lecture ----------

void Backend::search(const QString &text, const QString &source)
{
    const QString q = text.trimmed();
    if (q.isEmpty())
        return;
    m_searchSource = source;
    m_searching = true;
    m_searchError.clear();
    const quint64 seq = ++m_searchSeq;
    m_local.setTracks(m_db.searchLocal(q, 6));
    emit searchChanged();

    auto finish = [this, seq](QList<Track> tracks, QString err) {
        if (seq != m_searchSeq)
            return;  // une recherche plus récente a été lancée
        // Signale ce qui est déjà connu localement.
        for (Track &t : tracks) {
            const auto known = t.ytId.isEmpty() ? m_db.findLocalMatch(t.spotifyId, t.artist, t.title) : m_db.trackByYt(t.ytId);
            if (known) {
                if (t.ytId.isEmpty())
                    t.ytId = known->ytId;
                t.id = known->id;
                t.state = known->state;
                t.filePath = known->filePath;
                t.codec = known->codec;
                t.bitrate = known->bitrate;
            }
        }
        m_search.setTracks(tracks);
        m_searching = false;
        m_searchError = err;
        emit searchChanged();
    };
    // Lien YouTube collé directement.
    const QString vid = q.contains("youtu") ? videoIdFrom(q) : QString();
    if (!vid.isEmpty()) {
        Track t;
        t.ytId = vid;
        finish({m_db.upsertTrack(t)}, {});
        return;
    }
    if (source == "spotify")
        m_spotify->search(q, finish);
    else
        m_yt->search(q, 20, finish);
}

void Backend::play(const QString &list, int row)
{
    TrackModel *m = model(list);
    const Track *t = m ? m->at(row) : nullptr;
    if (!t)
        return;
    if (list == "jam") {
        m_jam->jumpTo(row);
        return;
    }
    if (m_jam->active()) {
        addToJam({*t});
        return;
    }
    if (list == "queue") {
        m_player->jump(row);
        return;
    }
    if (list == "playlist") {
        // Toute la playlist est lue, même filtrée : on part du morceau choisi.
        const QList<Track> all = m_db.playlistTracks(m_currentPlaylistId);
        int start = row;
        if (!m_playlistFilter.isEmpty())
            for (int i = 0; i < all.size(); ++i)
                if (all[i].id == t->id) {
                    start = i;
                    break;
                }
        m_playingPlaylistId = m_currentPlaylistId;
        m_player->playList(all, start, m_player->shuffle());
        return;
    }
    // Recherche : les résultats suivants déjà identifiés (YouTube) forment la file.
    QList<Track> context;
    // (sans les vidéos de plus de 15 min : compilations, lives complets…).
    for (int i = row + 1; i < m->count(); ++i)
        if (!m->at(i)->ytId.isEmpty() && m->at(i)->duration < 15 * 60)
            context.append(*m->at(i));
    if (t->ytId.isEmpty())
        emit toast(QStringLiteral("Recherche de la meilleure source sur YouTube…"), false);
    resolve(*t, [this, context](std::optional<Track> tr, QString err) {
        if (!tr) {
            emit toast(err, true);
            return;
        }
        QList<Track> list{*tr};
        for (const Track &c : context) {
            const Track r = m_db.upsertTrack(c);
            if (r.id != tr->id)
                list.append(r);
        }
        // Les résultats affichés connaissent désormais leur id local (surbrillance, menus).
        for (const Track &r : std::as_const(list)) {
            m_search.updateTrack(r);
            m_local.updateTrack(r);
        }
        m_playingPlaylistId = 0;
        // Les résultats suivants ne sont pas téléchargés d'avance : seul leur flux est préparé.
        m_player->playList(list, 0, m_player->shuffle(), true);
    });
}

void Backend::trackAction(const QString &list, int row, const QString &action, const QVariant &arg)
{
    TrackModel *m = model(list);
    const Track *tp = m ? m->at(row) : nullptr;
    if (!tp)
        return;
    const Track t = *tp;
    if (action == "removeFromJam") {
        m_jam->removeAt(row);
    } else if ((action == "next" || action == "enqueue") && m_jam->active()) {
        addToJam({t});
    } else if (action == "next" || action == "enqueue") {
        const bool playNext = action == "next";
        resolve(t, [this, playNext](std::optional<Track> r, QString err) {
            if (!r) {
                emit toast(err, true);
                return;
            }
            m_player->enqueue(*r, playNext);
            emit toast(playNext ? QStringLiteral("Sera lu ensuite") : QStringLiteral("Ajouté à la file"), false);
        });
    } else if (action == "addTo") {
        const qint64 pid = arg.toLongLong();
        resolve(t, [this, pid](std::optional<Track> r, QString err) {
            if (!r) {
                emit toast(err, true);
                return;
            }
            const auto p = m_db.playlist(pid);
            setTargetPlaylist(pid);
            if (m_db.playlistsContaining(r->id, {}).value(pid, false)) {
                emit toast(QStringLiteral("Déjà dans « %1 »").arg(p ? p->name : QString()), false);
                updateMarks();
                emit playlistsChanged();
                return;
            }
            m_db.addToPlaylist(pid, r->id);
            // Une playlist personnelle sert à garder les morceaux : on les télécharge.
            if (m_db.settingBool("auto_cache", true))
                m_dl->enqueue(r->id);
            emit toast(QStringLiteral("Ajouté à « %1 »").arg(p ? p->name : QString()), false);
            refreshPlaylists();
        });
    } else if (action == "download") {
        resolve(t, [this](std::optional<Track> r, QString err) {
            if (!r) {
                emit toast(err, true);
                return;
            }
            m_dl->enqueue(r->id);
            emit toast(QStringLiteral("Téléchargement ajouté à la file"), false);
        });
    } else if (action == "deleteFile" && t.id > 0) {
        m_dl->removeFile(t.id);
        if (auto fresh = m_db.track(t.id))
            m->updateTrack(*fresh);
        scheduleRefresh();
        emit toast(QStringLiteral("Fichier local supprimé"), false);
    } else if (action == "copyLink" && !t.ytId.isEmpty()) {
        copyText(watchUrl(t.ytId));
        emit toast(QStringLiteral("Lien copié"), false);
    } else if (action == "removeFromPlaylist" && m_currentPlaylistId > 0) {
        m_db.removeFromPlaylist(m_currentPlaylistId, t.id);
        // Retirer du cache supprime le fichier, sauf s'il sert dans une playlist personnelle.
        if (m_currentPlaylistId == m_db.cachePlaylistId() && !m_db.isPinned(t.id))
            m_dl->removeFile(t.id);
        openPlaylist(m_currentPlaylistId);
        scheduleRefresh();
    } else if (action == "removeFromQueue") {
        m_player->removeAt(row);
    } else if (action == "moveUp" || action == "moveDown") {
        const int to = action == "moveUp" ? row - 1 : row + 1;
        if (to < 0 || to >= m->count())
            return;
        if (list == "queue") {
            m_player->moveItem(row, to);
        } else if (list == "playlist" && m_playlistFilter.isEmpty()) {
            m_db.moveInPlaylist(m_currentPlaylistId, t.id, to);
            openPlaylist(m_currentPlaylistId);
        }
    }
}

// ---------- Commandes de lecture (locales, ou communes pendant un Jam) ----------

void Backend::togglePlay()
{
    m_jam->active() ? m_jam->togglePlay() : m_player->toggle();
}

void Backend::next()
{
    m_jam->active() ? m_jam->next() : m_player->next();
}

void Backend::prev()
{
    m_jam->active() ? m_jam->prev() : m_player->prev();
}

void Backend::seek(double pos)
{
    m_jam->active() ? m_jam->seek(pos) : m_player->seek(pos);
}

bool Backend::jamBlocks()
{
    if (m_jam->active())
        emit toast(QStringLiteral("Indisponible pendant un Jam"), false);
    return m_jam->active();
}

void Backend::toggleShuffle()
{
    if (!jamBlocks())
        m_player->setShuffle(!m_player->shuffle());
}

void Backend::cycleRepeat()
{
    if (!jamBlocks())
        m_player->setRepeat((m_player->repeat() + 1) % 3);
}

void Backend::clearQueue()
{
    if (!jamBlocks())
        m_player->clearQueue();
}

void Backend::addToJam(const QList<Track> &tracks)
{
    // Morceaux déjà identifiés : envoyés tels quels ; sinon (résultat Spotify) on
    // cherche d'abord la source YouTube, que chacun téléchargera de son côté.
    QList<Track> ready;
    for (const Track &t : tracks) {
        if (!t.ytId.isEmpty()) {
            ready.append(t);
            continue;
        }
        emit toast(QStringLiteral("Recherche de la meilleure source sur YouTube…"), false);
        resolve(t, [this](std::optional<Track> r, QString err) {
            if (!r) {
                emit toast(err, true);
                return;
            }
            if (m_jam->add({*r}))
                emit toast(QStringLiteral("Ajouté au Jam : %1").arg(r->title), false);
        });
    }
    if (ready.isEmpty())
        return;
    const int n = m_jam->add(ready);
    if (n == 1)
        emit toast(QStringLiteral("Ajouté au Jam : %1").arg(ready.first().title), false);
    else if (n > 1)
        emit toast(QStringLiteral("%1 morceaux ajoutés au Jam").arg(n), false);
}

// ---------- Playlists ----------

qint64 Backend::createPlaylist(const QString &name, const QString &addFromList, int row)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return 0;
    const qint64 id = m_db.createPlaylist(n, "user");
    setTargetPlaylist(id);
    if (!addFromList.isEmpty() && row >= 0)
        trackAction(addFromList, row, "addTo", id);
    refreshPlaylists();
    return id;
}

void Backend::openPlaylist(qint64 id)
{
    const auto p = m_db.playlist(id);
    if (!p)
        return;
    if (id != m_currentPlaylistId)
        m_playlistFilter.clear();
    m_currentPlaylistId = id;
    m_currentPlaylist = p->toMap();
    if (p->kind == "user" && id != m_targetPlaylistId) {
        // Playlist personnelle ouverte : c'est elle qu'on complète.
        setTargetPlaylist(id);
        updateMarks();
        emit playlistsChanged();
    }
    QList<Track> tracks = m_db.playlistTracks(id);
    if (!m_playlistFilter.isEmpty()) {
        const QStringList words = Matcher::normalize(m_playlistFilter).split(' ', Qt::SkipEmptyParts);
        tracks.removeIf([&words](const Track &t) {
            const QString hay = Matcher::normalize(t.title + ' ' + t.artist + ' ' + t.album);
            return !std::all_of(words.cbegin(), words.cend(), [&hay](const QString &w) { return hay.contains(w); });
        });
    }
    m_playlistTracks.setTracks(tracks);
    emit currentPlaylistChanged();
}

void Backend::filterPlaylist(const QString &text)
{
    const QString f = text.trimmed();
    if (f == m_playlistFilter)
        return;
    m_playlistFilter = f;
    if (m_currentPlaylistId > 0)
        openPlaylist(m_currentPlaylistId);
}

void Backend::playPlaylist(qint64 id, bool shuffleOn)
{
    auto tracks = m_db.playlistTracks(id);
    if (tracks.isEmpty()) {
        emit toast(QStringLiteral("Playlist vide"), true);
        return;
    }
    if (m_jam->active()) {
        // Pendant un Jam : la playlist rejoint la file commune (mélangée si demandé).
        if (shuffleOn)
            std::shuffle(tracks.begin(), tracks.end(), *QRandomGenerator::global());
        addToJam(tracks);
        return;
    }
    const int start = shuffleOn ? int(QRandomGenerator::global()->bounded(int(tracks.size()))) : 0;
    m_playingPlaylistId = id;
    m_player->playList(tracks, start, shuffleOn);
}

void Backend::shufflePlaylist(qint64 id)
{
    if (m_jam->active()) {
        playPlaylist(id, true);
        return;
    }
    const bool on = !m_player->shuffle();
    // Playlist en cours de lecture (ou rien à lire) : on bascule simplement le mode,
    // la file est mélangée en entier ou remise dans l'ordre de la playlist.
    if ((m_playingPlaylistId == id && m_player->current()) || !on) {
        m_player->setShuffle(on);
        return;
    }
    playPlaylist(id, true);
}

void Backend::renamePlaylist(qint64 id, const QString &name)
{
    if (name.trimmed().isEmpty())
        return;
    m_db.renamePlaylist(id, name.trimmed());
    refreshPlaylists();
}

void Backend::movePlaylist(qint64 id, int index)
{
    QList<qint64> ids;
    for (const Playlist &p : m_db.playlists())
        if (p.kind != QLatin1String("cache"))
            ids << p.id;
    const int from = int(ids.indexOf(id));
    // Les index de la barre latérale comptent « Cache local », toujours en premier.
    const int to = qBound(0, index - 1, int(ids.size()) - 1);
    if (from < 0 || from == to)
        return;
    ids.move(from, to);
    m_db.setPlaylistOrder(ids);
    refreshPlaylists();
}

void Backend::deletePlaylist(qint64 id)
{
    if (!m_db.deletePlaylist(id))
        return;
    // Les fichiers deviennent éligibles au nettoyage normal du cache.
    m_dl->enforceCacheLimit();
    if (m_currentPlaylistId == id)
        m_currentPlaylistId = 0;
    refreshPlaylists();
    emit toast(QStringLiteral("Playlist supprimée"), false);
}

void Backend::downloadPlaylist(qint64 id)
{
    int n = 0;
    for (const Track &t : m_db.playlistTracks(id))
        if (!t.isLocal()) {
            m_dl->enqueue(t.id);
            ++n;
        }
    emit toast(n ? QStringLiteral("%1 morceau(x) en file de téléchargement").arg(n)
                 : QStringLiteral("Tout est déjà en local"),
               false);
    openPlaylist(id);
}

// ---------- Comptes ----------

void Backend::loadRemote(const QString &kind)
{
    m_remoteLoading = true;
    m_remoteError.clear();
    emit remoteChanged();
    auto done = [this, kind](QList<RemotePlaylist> list, QString err) {
        QVariantList v;
        for (const RemotePlaylist &p : list)
            v.append(p.toMap());
        (kind == "youtube" ? m_remoteYoutube : m_remoteSpotify) = v;
        m_remoteError = err;
        m_remoteLoading = false;
        emit remoteChanged();
    };
    if (kind == "youtube")
        m_yt->accountPlaylists(done);
    else
        m_spotify->playlists(done);
}

void Backend::clearJobs()
{
    m_db.clearFinishedJobs();
    refreshJobs();
}

void Backend::spotifyLogin()
{
    emit toast(QStringLiteral("Connexion dans votre navigateur…"), false);
    m_spotify->login([this](QString err) {
        emit toast(err.isEmpty() ? QStringLiteral("Compte Spotify connecté") : err, !err.isEmpty());
        emit accountsChanged();
    });
}

void Backend::spotifyLogout()
{
    m_spotify->logout();
    emit accountsChanged();
}

void Backend::updateYtDlp()
{
    emit toast(QStringLiteral("Mise à jour de yt-dlp…"), false);
    m_yt->updateManaged([this](QString msg, bool ok) {
        emit toast(msg, !ok);
        m_yt->version([this](QString v) {
            m_ytdlpVersion = v;
            emit accountsChanged();
        });
    });
}

// ---------- Réglages ----------

void Backend::setSetting(const QString &key, const QString &value)
{
    if (value.isEmpty())
        m_db.removeSetting(key);
    else
        m_db.setSetting(key, value);
    if (key == "yt_cookies_browser")
        m_yt->setCookiesBrowser(value);
    if (key == "search_source") {
        m_searchSource = value;
        emit searchChanged();
    }
    if (key == "cache_max_mb")
        m_dl->enforceCacheLimit();
    emit accountsChanged();
}

void Backend::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

// ---------- Paroles ----------

int Backend::lyricsIndex() const
{
    // Légère avance : la position n'est relue que 4 fois par seconde.
    return m_lyrics->indexAt(m_player->position() + 0.25);
}

// ---------- Statistiques ----------

namespace {

QString listenTime(double seconds)
{
    const qint64 mins = qint64(seconds / 60);
    if (mins < 60)
        return QStringLiteral("%1 min").arg(mins);
    const qint64 h = mins / 60, m = mins % 60;
    return m ? QStringLiteral("%1 h %2 min").arg(h).arg(m) : QStringLiteral("%1 h").arg(h);
}

QString playsText(int n)
{
    return n > 1 ? QStringLiteral("%1 écoutes").arg(n) : QStringLiteral("%1 écoute").arg(n);
}

}  // namespace

void Backend::loadStats(int days)
{
    m_statsDays = days;
    const qint64 since = days > 0 ? nowSecs() - qint64(days) * 86400 : 0;
    const ListenStats st = m_db.listenStats(since);
    int artistCount = 0;
    const QList<TopArtist> artists = m_db.topArtists(since, 15, &artistCount);
    QList<Track> tracks;
    QStringList extra;
    for (const TopTrack &tt : m_db.topTracks(since, 50)) {
        tracks.append(tt.track);
        extra.append(playsText(tt.plays) + QStringLiteral(" · ") + listenTime(tt.seconds));
    }
    m_statsTracks.setTracks(tracks, extra);
    m_stats = {{"days", days},
               {"time", listenTime(st.seconds)},
               {"plays", st.plays},
               {"tracks", st.tracks},
               {"artists", artistCount}};
    m_statsArtists.clear();
    const int maxPlays = artists.isEmpty() ? 1 : artists.first().plays;
    for (const TopArtist &a : artists)
        m_statsArtists.append(QVariantMap{{"name", a.name},
                                          {"plays", a.plays},
                                          {"playsText", playsText(a.plays)},
                                          {"time", listenTime(a.seconds)},
                                          {"tracks", a.tracks},
                                          {"share", double(a.plays) / maxPlays}});
    emit statsChanged();
}
