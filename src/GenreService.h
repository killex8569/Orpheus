#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <functional>

#include "Database.h"

// Détection automatique des styles/genres (métadonnées locales + iTunes / Deezer en arrière-plan).
class GenreService : public QObject {
    Q_OBJECT
public:
    explicit GenreService(Database *db, QObject *parent = nullptr);

    static QStringList predefinedStyles();
    static QStringList inferFromMetadata(const QString &title, const QString &artist, const QString &album,
                                         const QStringList &playlistNames = {});
    static QStringList mapRawGenres(const QStringList &rawGenres);

    // Applique d'abord une détection locale rapide sur les morceaux sans style, puis
    // met en file la détection en ligne (iTunes / Deezer).
    void enqueuePlaylist(qint64 playlistId, bool applyHeuristicsFirst = true);
    void enqueueTrack(qint64 trackId, bool force = false);
    void enqueueMissing();

    bool running() const { return m_busy || !m_queue.isEmpty(); }
    int remaining() const { return int(m_queue.size()) + (m_busy ? 1 : 0); }

signals:
    void trackStylesChanged(qint64 trackId);
    void statusChanged();

private:
    void pump();
    void fetchItunes(const Track &t, std::function<void(QStringList genres, bool reached)> cb);
    void fetchDeezer(const Track &t, std::function<void(QStringList genres, bool reached)> cb);
    void finishTrack(const Track &t, const QStringList &apiGenres, bool reached);

    Database *m_db;
    QNetworkAccessManager m_net;
    QList<qint64> m_queue;
    bool m_busy = false;
    int m_failStreak = 0;
};
