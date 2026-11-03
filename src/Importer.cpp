#include "Importer.h"

#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <memory>

// ---------- Exécution parallèle ----------

namespace {

struct Pool {
    int count = 0, width = 1;
    int next = 0, finished = 0, inflight = 0;
    bool pumping = false, notified = false;
    std::function<void(int, std::function<void()>)> task;
    std::function<void()> onAll;
};

void pump(const std::shared_ptr<Pool> &p)
{
    // Une tâche terminée immédiatement rappelle pump : la boucle en cours s'en charge
    // (pas de récursion profonde sur les longues playlists déjà en local).
    if (p->pumping)
        return;
    p->pumping = true;
    while (p->inflight < p->width && p->next < p->count) {
        const int i = p->next++;
        ++p->inflight;
        // Le rappel garde le pool en vie jusqu'à la fin de la tâche (pas de cycle).
        p->task(i, [p] {
            --p->inflight;
            ++p->finished;
            pump(p);
        });
    }
    p->pumping = false;
    if (p->finished == p->count && !p->notified) {
        p->notified = true;
        p->onAll();
    }
}

}  // namespace

void runParallel(int count, int width, std::function<void(int, std::function<void()>)> task, std::function<void()> onAll)
{
    auto p = std::make_shared<Pool>();
    p->count = count;
    p->width = qMax(1, width);
    p->task = std::move(task);
    p->onAll = std::move(onAll);
    pump(p);
}

// ---------- Imports ----------

Importer::Importer(Database *db, YtDlp *yt, Spotify *spotify, SpotifyWeb *spotifyWeb, Downloader *dl, TrackResolver *resolver,
                   QObject *parent)
    : QObject(parent), m_db(db), m_yt(yt), m_spotify(spotify), m_spotifyWeb(spotifyWeb), m_dl(dl), m_resolver(resolver)
{
}

std::optional<Importer::Source> Importer::parse(const QString &url, QString *error) const
{
    if (url == "spotify:liked")
        return Source{"spotify", "playlist", "liked"};
    if (const auto sp = parseSpotifyUrl(url); !sp.first.isEmpty())
        return Source{"spotify", sp.first, sp.second};
    if (url.contains("youtube.com") || url.contains("youtu.be")) {
        const QString ref = QUrlQuery(QUrl(url).query()).queryItemValue("list");
        if (!ref.isEmpty())
            return Source{"youtube", {}, ref};
        *error = QStringLiteral("Le lien YouTube ne contient pas de playlist (paramètre list=)");
        return std::nullopt;
    }
    *error = QStringLiteral("Lien non reconnu : collez un lien de playlist YouTube ou Spotify");
    return std::nullopt;
}

void Importer::importUrl(const QString &rawUrl)
{
    const QString url = rawUrl.trimmed();
    QString error;
    const auto src = parse(url, &error);
    if (!src) {
        emit toast(error, true);
        return;
    }
    const qint64 job = m_db->createJob(src->source, url);
    emit jobsChanged();
    emit toast(QStringLiteral("Import lancé"), false);
    run(job, *src, url, {}, [] {});
}

void Importer::importMany(const QList<QPair<QString, QString>> &items, const ImportOptions &options)
{
    int added = 0;
    for (const auto &[rawUrl, title] : items) {
        const QString url = rawUrl.trimmed();
        QString error;
        const auto src = parse(url, &error);
        if (!src) {
            emit toast(error, true);
            continue;
        }
        // Le job porte déjà le nom de la playlist : la file d'attente reste lisible.
        m_queue.append({m_db->createJob(src->source, title.isEmpty() ? url : title), *src, url, options});
        ++added;
    }
    if (added == 0)
        return;
    emit jobsChanged();
    emit toast(added > 1 ? QStringLiteral("%1 imports en file : ils seront traités l'un après l'autre").arg(added)
                         : QStringLiteral("Import lancé"),
               false);
    startNext();
}

void Importer::startNext()
{
    if (m_queueBusy || m_queue.isEmpty())
        return;
    m_queueBusy = true;
    const Queued q = m_queue.takeFirst();
    run(q.job, q.src, q.url, q.options, [this] {
        // Rappel différé : l'import suivant ne démarre pas dans la pile de celui qui finit.
        QTimer::singleShot(0, this, [this] {
            m_queueBusy = false;
            startNext();
        });
    });
}

void Importer::syncPlaylist(qint64 id)
{
    const auto p = m_db->playlist(id);
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

void Importer::importTrackLinks(const QString &text, const QString &rawName)
{
    const QStringList ids = spotifyTrackIds(text);
    if (ids.isEmpty()) {
        emit toast(QStringLiteral("Aucun lien de morceau Spotify trouvé dans le texte collé"), true);
        return;
    }
    const QString name = rawName.trimmed().isEmpty() ? QStringLiteral("Playlist Spotify") : rawName.trimmed();
    struct State {
        ImportJob job;
        QList<std::optional<Track>> results;
        int finished = 0;
    };
    auto st = std::make_shared<State>();
    st->results.resize(ids.size());
    st->job.id = m_db->createJob("spotify", name);
    st->job.source = "spotify";
    st->job.name = name;
    st->job.total = int(ids.size());
    st->job.status = "running";
    st->job.message = QStringLiteral("Lecture des morceaux sur Spotify…");
    m_db->updateJob(st->job);
    emit jobsChanged();
    emit toast(QStringLiteral("Import de %1 morceau(x) lancé").arg(ids.size()), false);

    // Pages publiques des morceaux, quelques-unes à la fois, puis import normal.
    runParallel(
        int(ids.size()), 4,
        [this, st, ids](int i, std::function<void()> done) {
            m_spotify->publicTrack(ids[i], [this, st, i, total = ids.size(), done](std::optional<Track> t, QString) {
                st->results[i] = t;
                if (++st->finished % 10 == 0) {
                    st->job.message = QStringLiteral("Lecture des morceaux sur Spotify : %1/%2").arg(st->finished).arg(total);
                    m_db->updateJob(st->job);
                    emit progress();
                }
                done();
            });
        },
        [this, st, name] {
            QList<Track> tracks;
            for (const auto &r : std::as_const(st->results))
                if (r)
                    tracks.append(*r);
            if (tracks.isEmpty()) {
                st->job.status = "error";
                st->job.message = QStringLiteral("aucun morceau lisible sur Spotify");
                m_db->updateJob(st->job);
                emit jobsChanged();
                return;
            }
            // Même nom = même playlist : recoller la liste la met à jour sans doublon.
            importTracks(st->job.id, "spotify", "pasted:" + name.toLower(), name, tracks);
        });
}

void Importer::fail(qint64 jobId, const QString &err)
{
    ImportJob j;
    j.id = jobId;
    for (const ImportJob &e : m_db->jobs())
        if (e.id == jobId)
            j = e;
    j.status = "error";
    j.message = err;
    m_db->updateJob(j);
    emit jobsChanged();
}

void Importer::run(qint64 jobId, const Source &src, const QString &url, const ImportOptions &options,
                   std::function<void()> done)
{
    auto got = [this, jobId, src, options, done](QString name, QList<Track> tracks, QString err) {
        if (!err.isEmpty()) {
            fail(jobId, err);
            done();
            return;
        }
        importTracks(jobId, src.source, src.source == "youtube" ? src.ref : src.kind + ":" + src.ref, name, tracks, options,
                     done);
    };
    if (src.source == "youtube")
        m_yt->playlist(url, got);
    else
        runSpotify(src, got);
}

void Importer::runSpotify(const Source &src, std::function<void(QString, QList<Track>, QString)> got)
{
    const QString kind = src.kind, ref = src.ref;
    if (ref == "liked") {
        // Session du lecteur web (compte gratuit) d'abord, puis application développeur.
        if (m_spotifyWeb->isConnected())
            m_spotifyWeb->likedTracks(got);
        else if (m_spotify->isConnected())
            m_spotify->tracks(kind, ref, got);
        else
            got({}, {}, QStringLiteral("connectez votre compte Spotify (Paramètres) pour importer les titres likés"));
        return;
    }
    // Replis : API officielle (application développeur), puis page d'intégration (~100 titres).
    auto fallback = [this, kind, ref, got] {
        if (!m_spotify->isConnected()) {
            m_spotify->publicTracks(kind, ref, got);
            return;
        }
        m_spotify->tracks(kind, ref, [this, kind, ref, got](QString name, QList<Track> tracks, QString err) {
            // Playlist d'un tiers inaccessible via l'API : page publique en repli.
            if (!err.isEmpty())
                m_spotify->publicTracks(kind, ref, got);
            else
                got(name, tracks, err);
        });
    };
    if (kind != "playlist") {
        fallback();
        return;
    }
    // Lecteur web : playlist entière, privée comprise avec un compte connecté (même gratuit).
    m_spotifyWeb->playlistTracks(ref, [got, fallback](QString name, QList<Track> tracks, QString err) {
        if (err.isEmpty()) {
            got(name, tracks, {});
            return;
        }
        qWarning() << "Spotify (lecteur web) :" << err << "— repli sur les autres méthodes";
        fallback();
    });
}

void Importer::importTracks(qint64 jobId, const QString &kind, const QString &ref, const QString &name, const QList<Track> &tracks,
                            const ImportOptions &options, std::function<void()> done)
{
    // Ré-importer la même source met à jour sa playlist (synchronisation sans doublons).
    qint64 playlistId = m_db->playlistBySource(kind, ref).value_or(0);
    if (playlistId)
        m_db->renamePlaylist(playlistId, name);
    else
        playlistId = m_db->createPlaylist(name, kind, ref);
    // Dossier choisi : la playlist range ses fichiers dans <dossier>/<nom de la playlist>,
    // avant tout téléchargement (les fichiers y arrivent directement).
    if (!options.parentFolder.isEmpty()) {
        QString sub = cleanFileName(name, 100);
        if (sub.isEmpty())
            sub = QStringLiteral("Playlist");
        const QString dir = QDir::cleanPath(options.parentFolder + "/" + sub);
        const auto p = m_db->playlist(playlistId);
        if (p && p->folder != dir) {
            if (QDir().mkpath(dir))
                m_db->setPlaylistFolder(playlistId, dir);
            else
                emit toast(QStringLiteral("Impossible de créer le dossier « %1 » : fichiers gardés dans la bibliothèque")
                               .arg(QDir::toNativeSeparators(dir)),
                           true);
        }
    }
    emit playlistsChanged();

    struct State {
        ImportJob job;
        QList<Track> inputs;
        QList<std::optional<Track>> results;
        QList<bool> resolved;
        int added = 0;
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
    m_db->updateJob(st->job);
    emit jobsChanged();

    const bool download = options.download.value_or(m_db->settingBool("import_download", true));
    const qint64 cache = m_db->cachePlaylistId();
    // Ajoute à la playlist les morceaux résolus, dans l'ordre d'origine.
    auto flush = [this, st, playlistId, cache, download] {
        while (st->added < st->inputs.size() && st->resolved[st->added]) {
            if (const auto &r = st->results[st->added]) {
                m_db->addToPlaylist(playlistId, r->id);
                m_db->addToPlaylist(cache, r->id);
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
    };
    // Résolution en parallèle (3 à la fois).
    runParallel(
        int(tracks.size()), 3,
        [this, st, flush](int i, std::function<void()> done) {
            m_resolver->resolve(st->inputs[i], [this, st, flush, i, done](std::optional<Track> t, QString) {
                st->results[i] = t;
                st->resolved[i] = true;
                flush();
                m_db->updateJob(st->job);
                emit progress();
                done();
            });
        },
        [this, st, playlistId, done] {
            st->job.status = "done";
            st->job.message = st->missing.isEmpty() ? QString() : "Introuvables : " + st->missing.join(" · ");
            m_db->updateJob(st->job);
            emit jobsChanged();
            emit finished(playlistId);
            emit toast(QStringLiteral("« %1 » importée : %2 morceaux").arg(st->job.name).arg(st->job.done), false);
            if (done)
                done();
        });
}
