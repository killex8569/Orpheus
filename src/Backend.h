#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "Database.h"
#include "Downloader.h"
#include "Importer.h"
#include "JamController.h"
#include "LyricsService.h"
#include "Paths.h"
#include "Player.h"
#include "Resolver.h"
#include "Spotify.h"
#include "TrackModel.h"
#include "UpdateChecker.h"
#include "YtDlp.h"

// Pont entre la logique de l'application et l'interface QML. Les imports
// (Importer), les paroles (LyricsService) et la résolution des morceaux
// (TrackResolver) vivent dans leurs propres classes.
class Backend : public QObject {
    Q_OBJECT
    // Listes
    Q_PROPERTY(TrackModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(TrackModel *localResults READ localResults CONSTANT)
    Q_PROPERTY(TrackModel *playlistTracks READ playlistTracks CONSTANT)
    Q_PROPERTY(TrackModel *queueModel READ queueModel CONSTANT)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(QVariantMap currentPlaylist READ currentPlaylist NOTIFY currentPlaylistChanged)
    // Texte de la barre de recherche de la playlist ouverte ("" = tous les morceaux).
    Q_PROPERTY(QString playlistFilter READ playlistFilter NOTIFY currentPlaylistChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(QString searchSource READ searchSource NOTIFY searchChanged)
    // Lecteur
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(qint64 currentTrackId READ currentTrackId NOTIFY trackChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString cover READ cover NOTIFY trackChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playerChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY playerChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY playerChanged)
    Q_PROPERTY(double volume READ volume NOTIFY playerChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY playerChanged)
    Q_PROPERTY(int repeat READ repeat NOTIFY playerChanged)
    Q_PROPERTY(QString source READ source NOTIFY playerChanged)
    Q_PROPERTY(QString status READ status NOTIFY playerChanged)
    Q_PROPERTY(QString quality READ quality NOTIFY playerChanged)
    Q_PROPERTY(double cacheProgress READ cacheProgress NOTIFY positionChanged)
    // Divers
    Q_PROPERTY(QString downloadsText READ downloadsText NOTIFY downloadsChanged)
    Q_PROPERTY(int downloadsCount READ downloadsCount NOTIFY downloadsChanged)
    Q_PROPERTY(bool spotifyConnected READ spotifyConnected NOTIFY accountsChanged)
    Q_PROPERTY(QString spotifyUser READ spotifyUser NOTIFY accountsChanged)
    Q_PROPERTY(QString spotifyRedirect READ spotifyRedirect CONSTANT)
    Q_PROPERTY(QString ytdlpInfo READ ytdlpInfo NOTIFY accountsChanged)
    Q_PROPERTY(bool hasDeno READ hasDeno CONSTANT)
    Q_PROPERTY(QString cacheInfo READ cacheInfo NOTIFY downloadsChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QVariantList remoteYoutube READ remoteYoutube NOTIFY remoteChanged)
    Q_PROPERTY(QVariantList remoteSpotify READ remoteSpotify NOTIFY remoteChanged)
    Q_PROPERTY(QString remoteError READ remoteError NOTIFY remoteChanged)
    Q_PROPERTY(bool remoteLoading READ remoteLoading NOTIFY remoteChanged)
    // Playlist dans laquelle on range des morceaux (dernière utilisée) : ses morceaux
    // sont grisés dans les résultats de recherche pour éviter les doublons.
    Q_PROPERTY(QString targetPlaylistName READ targetPlaylistName NOTIFY playlistsChanged)
    Q_PROPERTY(qint64 playingPlaylistId READ playingPlaylistId NOTIFY trackChanged)
    // Paroles du morceau en cours. lyricsStatus : "" | loading | synced | plain | instrumental | missing | offline
    Q_PROPERTY(QVariantList lyricsLines READ lyricsLines NOTIFY lyricsChanged)
    Q_PROPERTY(QString lyricsPlain READ lyricsPlain NOTIFY lyricsChanged)
    Q_PROPERTY(QString lyricsStatus READ lyricsStatus NOTIFY lyricsChanged)
    Q_PROPERTY(int lyricsIndex READ lyricsIndex NOTIFY positionChanged)
    // Statistiques (période choisie avec loadStats)
    Q_PROPERTY(TrackModel *statsTracks READ statsTracks CONSTANT)
    // Jam : écoute synchronisée sur le réseau local.
    Q_PROPERTY(JamController *jam READ jam CONSTANT)
    // Mises à jour (releases GitHub).
    Q_PROPERTY(UpdateChecker *updates READ updates CONSTANT)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList statsArtists READ statsArtists NOTIFY statsChanged)

public:
    explicit Backend(const Paths &paths, QObject *parent = nullptr);
    ~Backend() override;
    bool init(QString *error);

    TrackModel *searchResults() { return &m_search; }
    TrackModel *localResults() { return &m_local; }
    TrackModel *playlistTracks() { return &m_playlistTracks; }
    TrackModel *queueModel() { return &m_queue; }
    QVariantList playlists() const { return m_playlists; }
    QVariantMap currentPlaylist() const { return m_currentPlaylist; }
    QString playlistFilter() const { return m_playlistFilter; }
    QVariantList jobs() const { return m_jobs; }
    bool searching() const { return m_searching; }
    QString searchError() const { return m_searchError; }
    QString searchSource() const { return m_searchSource; }

    bool hasTrack() const { return m_player->current().has_value(); }
    qint64 currentTrackId() const { return m_player->current() ? m_player->current()->id : 0; }
    int queueIndex() const { return m_player->index(); }
    QString title() const { return m_player->current() ? m_player->current()->title : QString(); }
    QString artist() const { return m_player->current() ? m_player->current()->artist : QString(); }
    QString album() const { return m_player->current() ? m_player->current()->album : QString(); }
    QString cover() const;
    bool paused() const { return m_player->paused(); }
    bool loading() const { return m_player->loading(); }
    double position() const { return m_player->position(); }
    double duration() const { return m_player->duration(); }
    double volume() const { return m_player->volume(); }
    bool shuffle() const { return m_player->shuffle(); }
    int repeat() const { return m_player->repeat(); }
    QString source() const { return m_player->source(); }
    QString status() const { return m_player->status(); }
    QString quality() const;
    double cacheProgress() const;

    QString downloadsText() const;
    int downloadsCount() const { return m_dl->activeCount() + m_dl->queuedCount(); }
    bool spotifyConnected() const { return m_spotify->isConnected(); }
    QString spotifyUser() const { return m_spotify->userName(); }
    QString spotifyRedirect() const { return Spotify::RedirectUri; }
    QString ytdlpInfo() const;
    bool hasDeno() const { return m_yt->hasDeno(); }
    QString cacheInfo() const;
    QString dataDir() const { return m_paths.data; }
    QVariantList remoteYoutube() const { return m_remoteYoutube; }
    QVariantList remoteSpotify() const { return m_remoteSpotify; }
    QString remoteError() const { return m_remoteError; }
    bool remoteLoading() const { return m_remoteLoading; }
    QString targetPlaylistName() const;
    qint64 playingPlaylistId() const { return m_playingPlaylistId; }
    QVariantList lyricsLines() const { return m_lyrics->lines(); }
    QString lyricsPlain() const { return m_lyrics->plain(); }
    QString lyricsStatus() const { return m_lyrics->status(); }
    int lyricsIndex() const;
    TrackModel *statsTracks() { return &m_statsTracks; }
    JamController *jam() const { return m_jam; }
    UpdateChecker *updates() const { return m_updates; }
    QVariantMap stats() const { return m_stats; }
    QVariantList statsArtists() const { return m_statsArtists; }

    // Recherche et lecture
    Q_INVOKABLE void search(const QString &text, const QString &source);
    Q_INVOKABLE void play(const QString &list, int row);
    Q_INVOKABLE void trackAction(const QString &list, int row, const QString &action, const QVariant &arg = {});
    // Pendant un Jam, ces commandes s'appliquent à tous les participants.
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void seek(double pos);
    Q_INVOKABLE void seekRelative(double delta) { seek(m_player->position() + delta); }
    Q_INVOKABLE void setVolume(double v) { m_player->setVolume(v); }
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    void setShuffle(bool on) { m_player->setShuffle(on); }
    void setRepeat(int mode) { m_player->setRepeat(mode); }
    bool canGoNext() const { return m_player->index() + 1 < m_player->queue().size() || (m_player->repeat() != Player::RepeatOff && hasTrack()); }
    bool canGoPrevious() const { return hasTrack(); }
    Q_INVOKABLE void clearQueue();

    // Playlists
    Q_INVOKABLE qint64 createPlaylist(const QString &name, const QString &addFromList = {}, int row = -1);
    Q_INVOKABLE void openPlaylist(qint64 id);
    // Filtre la playlist ouverte : chaque mot doit apparaître dans le titre, l'artiste
    // ou l'album (sans tenir compte de la casse ni des accents).
    Q_INVOKABLE void filterPlaylist(const QString &text);
    Q_INVOKABLE void playPlaylist(qint64 id, bool shuffle);
    // Bouton « Aléatoire » d'une playlist : active/désactive le mélange de toute la playlist.
    Q_INVOKABLE void shufflePlaylist(qint64 id);
    // {id de playlist: "same" | "similar"} pour le morceau d'une liste (menu « Ajouter à »).
    Q_INVOKABLE QVariantMap playlistMarks(const QString &list, int row);
    Q_INVOKABLE void renamePlaylist(qint64 id, const QString &name);
    // Déplace une playlist à la position « index » de la barre latérale (« Cache local » reste en tête).
    Q_INVOKABLE void movePlaylist(qint64 id, int index);
    Q_INVOKABLE void deletePlaylist(qint64 id);
    Q_INVOKABLE void downloadPlaylist(qint64 id);
    Q_INVOKABLE void syncPlaylist(qint64 id) { m_importer->syncPlaylist(id); }

    // Imports et comptes
    Q_INVOKABLE void importUrl(const QString &url) { m_importer->importUrl(url); }
    // Liens de morceaux Spotify collés (Ctrl+A, Ctrl+C dans une playlist, même privée).
    Q_INVOKABLE void importTrackLinks(const QString &text, const QString &name) { m_importer->importTrackLinks(text, name); }
    Q_INVOKABLE void loadRemote(const QString &kind);
    Q_INVOKABLE void clearJobs();
    Q_INVOKABLE void spotifyLogin();
    Q_INVOKABLE void spotifyLogout();
    Q_INVOKABLE void updateYtDlp();

    // Statistiques : days = 7, 30, 365… ou 0 pour tout l'historique.
    Q_INVOKABLE void loadStats(int days);

    // Réglages
    Q_INVOKABLE QString setting(const QString &key, const QString &def = {}) const { return m_db.setting(key, def); }
    Q_INVOKABLE bool settingBool(const QString &key, bool def) const { return m_db.settingBool(key, def); }
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);
    Q_INVOKABLE void copyText(const QString &text);

signals:
    void playlistsChanged();
    void currentPlaylistChanged();
    void jobsChanged();
    void searchChanged();
    void trackChanged();
    void playerChanged();
    void positionChanged();
    void seeked();  // saut dans le morceau (pour MPRIS)
    void downloadsChanged();
    void accountsChanged();
    void remoteChanged();
    void lyricsChanged();
    void statsChanged();
    void toast(const QString &message, bool error);

private:
    void resolve(const Track &input, TrackResolver::Callback cb) { m_resolver->resolve(input, std::move(cb)); }
    TrackModel *model(const QString &list);
    void refreshPlaylists();
    void refreshJobs();
    void scheduleRefresh();
    void setTargetPlaylist(qint64 id);
    void updateMarks();
    // Ajoute des morceaux à la file du Jam (après résolution si besoin).
    void addToJam(const QList<Track> &tracks);
    bool jamBlocks();  // action locale impossible pendant un Jam (avec message)

    Paths m_paths;
    Database m_db;
    YtDlp *m_yt;
    Downloader *m_dl;
    Spotify *m_spotify;
    Player *m_player;
    std::unique_ptr<TrackResolver> m_resolver;
    Importer *m_importer;
    LyricsService *m_lyrics;
    JamController *m_jam = nullptr;
    UpdateChecker *m_updates = nullptr;
    TrackModel m_search, m_local, m_playlistTracks, m_queue, m_statsTracks;
    QVariantList m_playlists, m_jobs, m_remoteYoutube, m_remoteSpotify;
    QVariantMap m_currentPlaylist;
    qint64 m_currentPlaylistId = 0;
    QString m_playlistFilter;
    qint64 m_playingPlaylistId = 0;
    qint64 m_targetPlaylistId = 0;
    bool m_searching = false;
    QString m_searchError, m_searchSource;
    quint64 m_searchSeq = 0;
    QString m_remoteError;
    bool m_remoteLoading = false;
    QString m_ytdlpVersion;
    // Statistiques
    int m_statsDays = 30;
    QVariantMap m_stats;
    QVariantList m_statsArtists;
    QTimer m_positionTimer, m_refreshTimer;
};
