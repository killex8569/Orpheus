#pragma once

#include <QFileInfo>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

// Morceau : connu en base (id > 0) ou simple résultat de recherche/import.
struct Track {
    qint64 id = 0;
    QString ytId;
    QString title;
    QString artist;
    QString album;
    double duration = 0;  // secondes, 0 = inconnue
    QString thumbnail;
    QString filePath;
    QString codec;
    double bitrate = 0;
    qint64 fileSize = 0;
    QString state = QStringLiteral("remote");  // remote | queued | downloading | ready | failed
    QString error;
    QString spotifyId;
    qint64 playCount = 0;
    qint64 lastPlayed = 0;
    qint64 addedAt = 0;

    // Fichier local réellement présent sur le disque ?
    bool isLocal() const { return state == QLatin1String("ready") && !filePath.isEmpty() && QFileInfo::exists(filePath); }
    QString quality() const;
};

Q_DECLARE_METATYPE(Track)

// Playlist distante (compte YouTube/Spotify) proposée à l'import.
struct RemotePlaylist {
    QString id;
    QString title;
    QString url;
    int count = -1;
    QVariantMap toMap() const;
};

QString formatTime(double seconds);
QString thumbUrl(const QString &ytId);
QString watchUrl(const QString &ytId);
// Extrait l'id de vidéo d'une URL YouTube (ou renvoie la chaîne si c'en est un).
QString videoIdFrom(const QString &s);
