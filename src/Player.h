#pragma once

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "Database.h"
#include "Downloader.h"
#include "YtDlp.h"

struct mpv_handle;

// Lecteur audio basé sur libmpv (intégré au processus).
// Stratégie de chargement d'un morceau :
//  1. fichier local présent → lecture immédiate ;
//  2. sinon téléchargement prioritaire (un seul transfert, ~2-4 s) puis lecture locale ;
//  3. si le téléchargement échoue ou traîne, bascule automatique en streaming.
// Morceau suivant : téléchargé d'avance (playlist, file), sauf les résultats de recherche
// qui suivent le morceau choisi : seul leur flux est préparé, rien n'est enregistré tant
// qu'ils ne sont pas réellement écoutés.
class Player : public QObject {
    Q_OBJECT
public:
    enum Repeat { RepeatOff = 0, RepeatAll = 1, RepeatOne = 2 };

    Player(Database *db, Downloader *dl, YtDlp *yt, QObject *parent = nullptr);
    ~Player() override;

    // État
    const QList<Track> &queue() const { return m_queue; }
    int index() const { return m_index; }
    std::optional<Track> current() const;
    bool paused() const { return m_paused || m_source.isEmpty(); }
    bool loading() const { return m_loading; }
    double position() const;
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    bool shuffle() const { return m_shuffle; }
    int repeat() const { return m_repeat; }
    QString source() const { return m_source; }
    QString status() const { return m_status; }
    int queueVersion() const { return m_queueVersion; }
    // Morceau chargé et prêt à jouer (fichier ou flux ouvert).
    bool isReady() const { return !m_source.isEmpty() && !m_loading; }

    // Commandes
    // browseRest : file issue d'une recherche, les morceaux autres que celui choisi ne
    // sont pas téléchargés d'avance.
    void playList(const QList<Track> &tracks, int start, bool shuffle, bool browseRest = false);
    void jump(int index);
    void next();
    void prev();
    void toggle();
    void seek(double pos);
    void setVolume(double v);
    void setShuffle(bool on);
    void setRepeat(int r);
    void enqueue(const Track &t, bool playNext);
    void removeAt(int index);
    void moveItem(int from, int to);
    void clearQueue();
    void refreshTrack(const Track &t);

    // Pilotage externe (Jam) : pas d'enchaînement automatique, chargement en pause
    // (le départ est donné par le Jam), aléatoire et répétition désactivés.
    void setExternalControl(bool on);
    void setPaused(bool paused);
    // Vitesse de lecture (légers ajustements pour rattraper un décalage).
    void setSpeed(double speed);
    void stop();
    // Remplace la file sans recharger le morceau en cours s'il ne change pas.
    void syncQueue(const QList<Track> &tracks, int index);

signals:
    void stateChanged();
    void trackChanged();
    void queueChanged();
    void errorOccurred(const QString &message);
    // Morceau écouté à plus de 50 % : compté dans les statistiques.
    void playCounted(qint64 trackId);
    // Lecture repartie d'une nouvelle position (saut, ou début d'un morceau).
    void seeked();
    // Fin naturelle d'un morceau en pilotage externe (le Jam décide de la suite).
    void endReached(qint64 trackId);

private:
    void onMpvEvents();
    void load(int index, bool resetRetry);
    void loadFile(const QString &target, const QString &source, const QString &userAgent = {});
    void startStreaming(quint64 generation);
    void onDownloadFinished(qint64 trackId, bool ok, const QString &error);
    void onPlaybackError(const QString &msg);
    void advance(bool naturalEnd);
    void prefetchNext();
    void preResolve(const Track &t);
    void setStatus(const QString &s);
    void bumpQueue();
    void trackListening();
    void finishListening();
    void shuffleQueue();
    void unshuffleQueue();

    Database *m_db;
    Downloader *m_dl;
    YtDlp *m_yt;
    mpv_handle *m_mpv = nullptr;

    QList<Track> m_queue;
    QList<Track> m_unshuffled;  // ordre d'origine quand le mode aléatoire est actif
    int m_index = -1;
    int m_repeat = RepeatOff;
    bool m_shuffle = false;
    bool m_paused = false;
    bool m_loading = false;
    double m_duration = 0;
    double m_volume = 80;
    QString m_source;  // "local" | "stream" | "" (arrêté)
    QString m_status;
    quint64 m_generation = 0;
    qint64 m_entryId = -1;
    qint64 m_waitingDownload = 0;
    int m_consecutiveErrors = 0;
    bool m_retried = false;
    int m_queueVersion = 0;
    QTimer m_streamFallback;
    QTimer m_volumeSave;
    bool m_external = false;
    double m_speed = 1.0;  // volume enregistré une fois le curseur relâché
    // Temps réellement écouté du morceau en cours (sauts et pauses exclus).
    QTimer m_listenTimer;
    qint64 m_listenTrack = 0;
    double m_listened = 0;
    double m_lastPos = -1;
    qint64 m_playRow = 0;       // ligne de l'historique, une fois l'écoute comptée
    double m_savedListened = 0;
    // Résultats de recherche dans la file : flux préparé au lieu d'un téléchargement.
    QSet<qint64> m_browseOnly;
    struct PreStream {
        qint64 trackId = 0;
        QString url, userAgent;
        QDateTime at;
    } m_pre;
    qint64 m_preResolving = 0;
};
