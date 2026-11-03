#pragma once

#include <QObject>
#include <optional>

#include "Database.h"
#include "Downloader.h"
#include "Resolver.h"
#include "Spotify.h"
#include "SpotifyWeb.h"
#include "YtDlp.h"

// Choix faits au moment d'importer une sélection de playlists.
struct ImportOptions {
    std::optional<bool> download;  // vide : réglage « Télécharger les morceaux importés »
    QString parentFolder;          // chaque playlist dans <dossier>/<nom> ; "" : inchangé
};

// Imports de playlists (YouTube, Spotify, liens collés) et synchronisation.
// Chaque import est un « job » suivi en base ; ses morceaux sont résolus en parallèle
// puis ajoutés dans l'ordre d'origine.
class Importer : public QObject {
    Q_OBJECT
public:
    Importer(Database *db, YtDlp *yt, Spotify *spotify, SpotifyWeb *spotifyWeb, Downloader *dl, TrackResolver *resolver,
             QObject *parent = nullptr);

    // Lien de playlist/album YouTube ou Spotify (ou « spotify:liked »).
    void importUrl(const QString &url);
    // Sélection de playlists d'un compte (url, titre) : importées l'une après l'autre,
    // pour ne pas multiplier les recherches YouTube simultanées.
    void importMany(const QList<QPair<QString, QString>> &items, const ImportOptions &options);
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
    struct Source {
        QString source, kind, ref;  // youtube|spotify, playlist|album, identifiant
    };
    struct Queued {
        qint64 job = 0;
        Source src;
        QString url;
        ImportOptions options;
    };
    // Reconnaît un lien ; message d'erreur dans *error sinon.
    std::optional<Source> parse(const QString &url, QString *error) const;
    // done : appelé une fois l'import terminé (réussi ou non).
    void run(qint64 jobId, const Source &src, const QString &url, const ImportOptions &options, std::function<void()> done);
    void runSpotify(const Source &src, std::function<void(QString, QList<Track>, QString)> got);
    void importTracks(qint64 jobId, const QString &kind, const QString &ref, const QString &name, const QList<Track> &tracks,
                      const ImportOptions &options = {}, std::function<void()> done = {});
    void fail(qint64 jobId, const QString &error);
    void startNext();

    Database *m_db;
    YtDlp *m_yt;
    Spotify *m_spotify;
    SpotifyWeb *m_spotifyWeb;
    Downloader *m_dl;
    TrackResolver *m_resolver;
    QList<Queued> m_queue;
    bool m_queueBusy = false;
};

// Lance task(i, done) pour i dans [0, count), au plus `width` à la fois ; onAll est
// appelé une fois toutes les tâches terminées (chacune doit appeler done une fois).
void runParallel(int count, int width, std::function<void(int index, std::function<void()> done)> task,
                 std::function<void()> onAll);
