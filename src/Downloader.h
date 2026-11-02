#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>

#include "Database.h"
#include "Paths.h"
#include "YtDlp.h"

// File de téléchargement persistante.
//  - un téléchargement « prioritaire » (le morceau qu'on veut écouter) démarre
//    immédiatement et met en pause l'arrière-plan ;
//  - peu de téléchargements d'arrière-plan simultanés (réglage, 2 par défaut),
//    démarrés de façon espacée pour ne pas se faire brider par YouTube ;
//  - nouvelles tentatives espacées, reprise au redémarrage, limite de taille du cache.
class Downloader : public QObject {
    Q_OBJECT
public:
    Downloader(Database *db, YtDlp *yt, const Paths &paths, QObject *parent = nullptr);

    void resumePending();
    void enqueue(qint64 trackId);
    // Lance tout de suite (ou passe en priorité un téléchargement en cours).
    void prioritize(qint64 trackId);
    bool isActive(qint64 trackId) const { return m_active.contains(trackId); }
    double progressOf(qint64 trackId) const;
    int activeCount() const { return int(m_active.size()); }
    int queuedCount() const { return int(m_queue.size()); }
    QString activeTitle() const;
    void removeFile(qint64 trackId);
    void enforceCacheLimit();

signals:
    void finished(qint64 trackId, bool ok, const QString &error);
    void progress(qint64 trackId, double percent);
    void statusChanged();

private:
    struct Active {
        QPointer<QProcess> process;
        double progress = 0;
        bool priority = false;
        bool preempted = false;  // interrompu pour laisser passer un prioritaire
        int attempt = 1;
        QString title;
    };
    void start(qint64 id, bool priority);
    void onDone(qint64 id, const YtDlp::DownloadResult &r, const QString &err);
    void pump();
    bool canStartBackground() const;
    void cleanupPartial(const QString &ytId);

    Database *m_db;
    YtDlp *m_yt;
    Paths m_paths;
    QList<qint64> m_queue;
    QHash<qint64, Active> m_active;
    QHash<qint64, int> m_attempts;  // tentatives déjà faites (nouvel essai différé)
    bool m_pumpScheduled = false;
};
