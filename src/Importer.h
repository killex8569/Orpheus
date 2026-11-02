#pragma once

#include <QObject>

#include "Database.h"
#include "Downloader.h"
#include "Resolver.h"
#include "Spotify.h"
#include "YtDlp.h"

// Imports de playlists (YouTube, Spotify, liens collés) et synchronisation.
// Chaque import est un « job » suivi en base ; ses morceaux sont résolus en parallèle
// puis ajoutés dans l'ordre d'origine.
class Importer : public QObject {
    Q_OBJECT
public:
    Importer(Database *db, YtDlp *yt, Spotify *spotify, Downloader *dl, TrackResolver *resolver, QObject *parent = nullptr);

    // Lien de playlist/album YouTube ou Spotify (ou « spotify:liked »).
    void importUrl(const QString &url);
    // Liens de morceaux Spotify collés (Ctrl+A, Ctrl+C dans une playlist, même privée).
    void importTrackLinks(const QString &text, const QString &name);
    // Ré-importe la source d'une playlist importée (ajout des nouveaux morceaux).
    void syncPlaylist(qint64 id);

signals:
    void jobsChanged();             // état d'un job modifié (à afficher tout de suite)
    void progress();                // avancement (affichage différé suffisant)
    void playlistsChanged();        // playlist créée ou renommée
    void finished(qint64 playlistId);
    void toast(const QString &message, bool error);

private:
    void run(qint64 jobId, const QString &source, const QString &kind, const QString &ref, const QString &url);
    void importTracks(qint64 jobId, const QString &kind, const QString &ref, const QString &name, const QList<Track> &tracks);
    void fail(qint64 jobId, const QString &error);

    Database *m_db;
    YtDlp *m_yt;
    Spotify *m_spotify;
    Downloader *m_dl;
    TrackResolver *m_resolver;
};

// Lance task(i, done) pour i dans [0, count), au plus `width` à la fois ; onAll est
// appelé une fois toutes les tâches terminées (chacune doit appeler done une fois).
void runParallel(int count, int width, std::function<void(int index, std::function<void()> done)> task,
                 std::function<void()> onAll);
