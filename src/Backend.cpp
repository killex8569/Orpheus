#include "Backend.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QLocale>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <QUrl>
#include <algorithm>
#include <memory>

#include "Matcher.h"

Backend::Backend(const Paths &paths, QObject *parent)
    : QObject(parent), m_paths(paths), m_search(&m_paths), m_local(&m_paths), m_playlistTracks(&m_paths), m_queue(&m_paths),
      m_statsTracks(&m_paths)
{
}

bool Backend::init(QString *error)
{
    if (!m_db.open(m_paths.db, error))
        return false;
    m_yt = new YtDlp(m_paths.bin, this);
    m_yt->setCookiesBrowser(m_db.setting("yt_cookies_browser"));
    m_dl = new Downloader(&m_db, m_yt, m_paths, this);
    m_spotify = new Spotify(&m_db, this);
    m_player = new Player(&m_db, m_dl, m_yt, this);
    m_lyrics = new LyricsClient(this);
    m_searchSource = m_db.setting("search_source", "youtube");

    connect(m_player, &Player::stateChanged, this, &Backend::playerChanged);
    connect(m_player, &Player::trackChanged, this, &Backend::trackChanged);
    connect(m_player, &Player::trackChanged, this, &Backend::playerChanged);
    connect(m_player, &Player::queueChanged, this, [this] {
        m_queue.setTracks(m_player->queue());
        emit trackChanged();
    });
    connect(m_player, &Player::trackChanged, this, &Backend::loadLyricsForCurrent);
    connect(m_player, &Player::playCounted, this, [this] { loadStats(m_statsDays); });
    connect(m_player, &Player::errorOccurred, this, [this](const QString &m) { emit toast(m, true); });
    connect(m_dl, &Downloader::statusChanged, this, &Backend::downloadsChanged);
    connect(m_dl, &Downloader::progress, this, &Backend::downloadsChanged);
    connect(m_dl, &Downloader::finished, this, [this](qint64 id, bool ok, const QString &) {
        // Morceau gardé en local : ses paroles aussi, pour l'écoute hors ligne.
        if (ok)
            enqueueLyrics(id);
        if (const auto t = m_db.track(id)) {
            for (TrackModel *m : {&m_search, &m_local, &m_playlistTracks})
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
    QTimer::singleShot(15000, this, [this] {
        for (const Track &t : m_db.tracksNeedingLyrics(1000))
            enqueueLyrics(t.id);
    });
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
    if (!targetPlaylistName().isEmpty()) {
        for (const Track &t : m_db.playlistTracks(m_targetPlaylistId)) {
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
    return nullptr;
}

// ---------- Résolution ----------

void Backend::resolve(const Track &input, ResolveCb cb)
{
    if (input.id > 0) {
        if (auto t = m_db.track(input.id)) {
            cb(*t, {});
            return;
        }
    }
    if (!input.ytId.isEmpty()) {
        cb(m_db.upsertTrack(input), {});
        return;
    }
    if (input.title.isEmpty()) {
        cb(std::nullopt, QStringLiteral("morceau sans titre"));
        return;
    }
    // 1) Bibliothèque locale (id Spotify, clé artiste|titre, correspondances mémorisées).
    if (auto local = m_db.findLocalMatch(input.spotifyId, input.artist, input.title)) {
        Track merged = input;
        merged.ytId = local->ytId;
        cb(m_db.upsertTrack(merged), {});
        return;
    }
    // 2) Recherche YouTube avec score de correspondance.
    m_yt->findMatch(input.artist, input.title, input.duration, [this, input, cb](std::optional<Track> found, QString err) {
        if (!err.isEmpty() || !found) {
            cb(std::nullopt, err.isEmpty() ? QStringLiteral("aucune correspondance fiable sur YouTube pour « %1 — %2 »")
                                                 .arg(input.artist, input.title)
                                           : err);
            return;
        }
        m_db.rememberMatch(input.artist, input.title, found->ytId);
        Track merged = input;
        merged.ytId = found->ytId;
        if (merged.thumbnail.isEmpty())
            merged.thumbnail = found->thumbnail;
        if (merged.duration <= 0)
            merged.duration = found->duration;
        cb(m_db.upsertTrack(merged), {});
    });
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
        m_player->playList(list, 0, m_player->shuffle());
    });
}

void Backend::trackAction(const QString &list, int row, const QString &action, const QVariant &arg)
{
    TrackModel *m = model(list);
    const Track *tp = m ? m->at(row) : nullptr;
    if (!tp)
        return;
    const Track t = *tp;
    if (action == "next" || action == "enqueue") {
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
    const auto tracks = m_db.playlistTracks(id);
    if (tracks.isEmpty()) {
        emit toast(QStringLiteral("Playlist vide"), true);
        return;
    }
    const int start = shuffleOn ? int(QRandomGenerator::global()->bounded(int(tracks.size()))) : 0;
    m_playingPlaylistId = id;
    m_player->playList(tracks, start, shuffleOn);
}

void Backend::shufflePlaylist(qint64 id)
{
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

void Backend::syncPlaylist(qint64 id)
{
    const auto p = m_db.playlist(id);
    if (!p || p->sourceRef.isEmpty())
        return;
    if (p->sourceRef.startsWith("pasted:")) {
        emit toast(QStringLiteral("Pour mettre à jour, collez de nouveau les morceaux avec le même nom (Importer)"), false);
        return;
    }
    QString url;
    if (p->kind == "spotify") {
        const QString kind = p->sourceRef.section(':', 0, 0), ref = p->sourceRef.section(':', 1);
        url = ref == "liked" ? QStringLiteral("spotify:liked") : QStringLiteral("https://open.spotify.com/%1/%2").arg(kind, ref);
    } else if (p->sourceRef == "LM") {
        url = QStringLiteral("https://music.youtube.com/playlist?list=LM");
    } else {
        url = "https://www.youtube.com/playlist?list=" + p->sourceRef;
    }
    importUrl(url);
}

// ---------- Imports ----------

void Backend::importUrl(const QString &rawUrl)
{
    const QString url = rawUrl.trimmed();
    QString source, kind, ref;
    if (url == "spotify:liked") {
        source = "spotify";
        kind = "playlist";
        ref = "liked";
    } else if (const auto sp = parseSpotifyUrl(url); !sp.first.isEmpty()) {
        source = "spotify";
        kind = sp.first;
        ref = sp.second;
    } else if (url.contains("youtube.com") || url.contains("youtu.be")) {
        const QUrlQuery q(QUrl(url).query());
        ref = q.queryItemValue("list");
        if (ref.isEmpty()) {
            emit toast(QStringLiteral("Le lien YouTube ne contient pas de playlist (paramètre list=)"), true);
            return;
        }
        source = "youtube";
    } else {
        emit toast(QStringLiteral("Lien non reconnu : collez un lien de playlist YouTube ou Spotify"), true);
        return;
    }
    const qint64 job = m_db.createJob(source, url);
    refreshJobs();
    emit toast(QStringLiteral("Import lancé"), false);
    runImport(job, source, kind, ref, url);
}

void Backend::importTrackLinks(const QString &text, const QString &rawName)
{
    const QStringList ids = spotifyTrackIds(text);
    if (ids.isEmpty()) {
        emit toast(QStringLiteral("Aucun lien de morceau Spotify trouvé dans le texte collé"), true);
        return;
    }
    const QString name = rawName.trimmed().isEmpty() ? QStringLiteral("Playlist Spotify") : rawName.trimmed();
    struct State {
        ImportJob job;
        QStringList ids;
        QList<std::optional<Track>> results;
        int next = 0, finished = 0, inflight = 0;
    };
    auto st = std::make_shared<State>();
    st->ids = ids;
    st->results.resize(ids.size());
    st->job.id = m_db.createJob("spotify", name);
    st->job.source = "spotify";
    st->job.name = name;
    st->job.total = int(ids.size());
    st->job.status = "running";
    st->job.message = QStringLiteral("Lecture des morceaux sur Spotify…");
    m_db.updateJob(st->job);
    refreshJobs();
    emit toast(QStringLiteral("Import de %1 morceau(x) lancé").arg(ids.size()), false);

    // Pages publiques des morceaux, quelques-unes à la fois, puis import normal.
    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, st, pump, name] {
        if (st->finished == st->ids.size()) {
            QList<Track> tracks;
            for (const auto &r : std::as_const(st->results))
                if (r)
                    tracks.append(*r);
            if (tracks.isEmpty()) {
                st->job.status = "error";
                st->job.message = QStringLiteral("aucun morceau lisible sur Spotify");
                m_db.updateJob(st->job);
                refreshJobs();
                return;
            }
            // Même nom = même playlist : recoller la liste la met à jour sans doublon.
            importTracks(st->job.id, "spotify", "pasted:" + name.toLower(), name, tracks);
            return;
        }
        while (st->inflight < 4 && st->next < st->ids.size()) {
            const int i = st->next++;
            ++st->inflight;
            m_spotify->publicTrack(st->ids[i], [this, st, pump, i](std::optional<Track> t, QString) {
                st->results[i] = t;
                --st->inflight;
                ++st->finished;
                if (st->finished % 10 == 0) {
                    st->job.message = QStringLiteral("Lecture des morceaux sur Spotify : %1/%2").arg(st->finished).arg(st->ids.size());
                    m_db.updateJob(st->job);
                    scheduleRefresh();
                }
                (*pump)();
            });
        }
    };
    (*pump)();
}

void Backend::runImport(qint64 jobId, const QString &source, const QString &kind, const QString &ref, const QString &url)
{
    auto fail = [this, jobId](const QString &err) {
        ImportJob j;
        j.id = jobId;
        for (const ImportJob &e : m_db.jobs())
            if (e.id == jobId)
                j = e;
        j.status = "error";
        j.message = err;
        m_db.updateJob(j);
        refreshJobs();
    };
    if (source == "youtube") {
        m_yt->playlist(url, [this, jobId, ref, fail](QString name, QList<Track> tracks, QString err) {
            if (!err.isEmpty())
                fail(err);
            else
                importTracks(jobId, "youtube", ref, name, tracks);
        });
        return;
    }
    auto got = [this, jobId, kind, ref, fail](QString name, QList<Track> tracks, QString err) {
        if (!err.isEmpty())
            fail(err);
        else
            importTracks(jobId, "spotify", kind + ":" + ref, name, tracks);
    };
    if (m_spotify->isConnected()) {
        m_spotify->tracks(kind, ref, [this, kind, ref, got](QString name, QList<Track> tracks, QString err) {
            // Playlist d'un tiers inaccessible via l'API : page publique en repli.
            if (!err.isEmpty() && ref != "liked")
                m_spotify->publicTracks(kind, ref, got);
            else
                got(name, tracks, err);
        });
    } else if (ref == "liked") {
        fail(QStringLiteral("connectez votre compte Spotify pour importer les titres likés"));
    } else {
        m_spotify->publicTracks(kind, ref, got);
    }
}

void Backend::importTracks(qint64 jobId, const QString &kind, const QString &ref, const QString &name, const QList<Track> &tracks)
{
    // Ré-importer la même source met à jour sa playlist (synchronisation sans doublons).
    qint64 playlistId = m_db.playlistBySource(kind, ref).value_or(0);
    if (playlistId)
        m_db.renamePlaylist(playlistId, name);
    else
        playlistId = m_db.createPlaylist(name, kind, ref);
    refreshPlaylists();

    struct State {
        ImportJob job;
        QList<Track> inputs;
        QList<std::optional<Track>> results;
        QList<bool> resolved;
        int next = 0, added = 0, inflight = 0;
        QStringList missing;
    };
    auto st = std::make_shared<State>();
    st->job.id = jobId;
    st->job.source = kind;
    st->job.name = name;
    st->job.playlistId = playlistId;
    st->job.total = int(tracks.size());
    st->job.status = "running";
    st->inputs = tracks;
    st->results.resize(tracks.size());
    st->resolved.fill(false, tracks.size());
    m_db.updateJob(st->job);
    refreshJobs();

    const bool download = m_db.settingBool("import_download", true);
    const qint64 cache = m_db.cachePlaylistId();
    // Résolution en parallèle (3 à la fois), ajout dans l'ordre d'origine.
    auto pump = std::make_shared<std::function<void()>>();
    *pump = [this, st, pump, playlistId, cache, download] {
        while (st->added < st->inputs.size() && st->resolved[st->added]) {
            const auto &r = st->results[st->added];
            if (r) {
                m_db.addToPlaylist(playlistId, r->id);
                m_db.addToPlaylist(cache, r->id);
                if (download)
                    m_dl->enqueue(r->id);
                ++st->job.done;
            } else {
                ++st->job.failed;
                if (st->missing.size() < 50)
                    st->missing << st->inputs[st->added].artist + " — " + st->inputs[st->added].title;
            }
            ++st->added;
        }
        if (st->added == st->inputs.size()) {
            st->job.status = "done";
            st->job.message = st->missing.isEmpty() ? QString() : "Introuvables : " + st->missing.join(" · ");
            m_db.updateJob(st->job);
            refreshJobs();
            refreshPlaylists();
            if (m_currentPlaylistId == playlistId)
                openPlaylist(playlistId);
            emit toast(QStringLiteral("Import terminé : %1 morceaux").arg(st->job.done), false);
            return;
        }
        m_db.updateJob(st->job);
        scheduleRefresh();
        while (st->inflight < 3 && st->next < st->inputs.size()) {
            const int i = st->next++;
            ++st->inflight;
            resolve(st->inputs[i], [st, pump, i](std::optional<Track> t, QString) {
                st->results[i] = t;
                st->resolved[i] = true;
                --st->inflight;
                (*pump)();
            });
        }
    };
    (*pump)();
}

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
    if (m_lyricsStatus != QLatin1String("synced"))
        return -1;
    // Légère avance : la position n'est relue que 4 fois par seconde.
    return lyricIndexAt(m_lyricsParsed, m_player->position() + 0.25);
}

void Backend::loadLyricsForCurrent()
{
    const auto t = m_player->current();
    const qint64 id = t ? t->id : 0;
    if (id == m_lyricsTrackId)
        return;
    m_lyricsTrackId = id;
    m_lyricsParsed.clear();
    m_lyricsLines.clear();
    m_lyricsPlain.clear();
    m_lyricsStatus.clear();
    if (!t) {
        emit lyricsChanged();
        return;
    }
    // Déjà en base : disponible immédiatement, même hors ligne.
    if (const auto stored = m_db.lyrics(id); stored && (stored->status == "found" || nowSecs() - stored->fetchedAt < 7 * 86400)) {
        applyLyrics(*stored);
        return;
    }
    m_lyricsStatus = QStringLiteral("loading");
    emit lyricsChanged();
    Track query = *t;
    if (query.duration <= 0)
        query.duration = m_player->duration();
    fetchLyrics(query, [this, id](bool reached) {
        if (id != m_lyricsTrackId)
            return;  // l'utilisateur est passé à un autre morceau
        if (const auto stored = m_db.lyrics(id); reached && stored) {
            applyLyrics(*stored);
        } else {
            m_lyricsStatus = QStringLiteral("offline");
            emit lyricsChanged();
            enqueueLyrics(id);  // nouvel essai automatique en arrière-plan
        }
    });
}

void Backend::applyLyrics(const StoredLyrics &l)
{
    m_lyricsParsed.clear();
    m_lyricsLines.clear();
    m_lyricsPlain.clear();
    if (l.status != "found") {
        m_lyricsStatus = QStringLiteral("missing");
    } else if (l.instrumental) {
        m_lyricsStatus = QStringLiteral("instrumental");
    } else if (!l.synced.isEmpty() && !(m_lyricsParsed = parseLrc(l.synced)).isEmpty()) {
        m_lyricsStatus = QStringLiteral("synced");
        for (const LyricLine &line : std::as_const(m_lyricsParsed))
            m_lyricsLines.append(QVariantMap{{"time", line.time}, {"text", line.text}});
    } else if (!l.plain.trimmed().isEmpty()) {
        m_lyricsStatus = QStringLiteral("plain");
        m_lyricsPlain = l.plain.trimmed();
    } else {
        m_lyricsStatus = QStringLiteral("missing");
    }
    emit lyricsChanged();
}

void Backend::fetchLyrics(const Track &t, std::function<void(bool)> cb)
{
    m_lyrics->fetch(t.artist, t.title, t.duration, [this, id = t.id, cb](LyricsClient::Result r) {
        // Hors ligne : rien n'est enregistré, on réessaiera plus tard.
        if (r.reached) {
            StoredLyrics l;
            l.status = r.found ? QStringLiteral("found") : QStringLiteral("missing");
            l.synced = r.synced;
            l.plain = r.plain;
            l.instrumental = r.instrumental;
            m_db.saveLyrics(id, l);
        }
        cb(r.reached);
    });
}

void Backend::enqueueLyrics(qint64 trackId)
{
    if (trackId <= 0 || m_lyricsQueue.contains(trackId))
        return;
    if (const auto stored = m_db.lyrics(trackId); stored && (stored->status == "found" || nowSecs() - stored->fetchedAt < 7 * 86400))
        return;
    m_lyricsQueue.append(trackId);
    pumpLyrics();
}

void Backend::pumpLyrics()
{
    if (m_lyricsBusy || m_lyricsQueue.isEmpty())
        return;
    const auto t = m_db.track(m_lyricsQueue.takeFirst());
    if (!t) {
        pumpLyrics();
        return;
    }
    m_lyricsBusy = true;
    fetchLyrics(*t, [this, id = t->id](bool reached) {
        m_lyricsBusy = false;
        if (!reached) {
            // Réessayé plus tard (3 fois au plus par session), sans bloquer les suivants.
            if (++m_lyricsFailures[id] < 3)
                m_lyricsQueue.append(id);
            // Plusieurs échecs d'affilée : pas de réseau, pause de 5 minutes.
            const int wait = ++m_lyricsFailStreak >= 3 ? 5 * 60 * 1000 : 5000;
            if (wait > 5000)
                m_lyricsFailStreak = 0;
            QTimer::singleShot(wait, this, &Backend::pumpLyrics);
            return;
        }
        m_lyricsFailStreak = 0;
        // Le morceau en cours attendait peut-être ces paroles.
        if (id == m_lyricsTrackId && m_lyricsStatus != "synced" && m_lyricsStatus != "plain")
            if (const auto stored = m_db.lyrics(id))
                applyLyrics(*stored);
        // Espacé, par politesse envers LRCLIB (service gratuit).
        QTimer::singleShot(1500, this, &Backend::pumpLyrics);
    });
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
