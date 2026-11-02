#pragma once

#include <QAbstractListModel>
#include <QSet>

#include "Paths.h"
#include "Track.h"

// Liste de morceaux affichée par l'interface (recherche, playlist, file d'attente).
class TrackModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        YtIdRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationTextRole,
        CoverRole,
        StateRole,
        QualityRole,
        ErrorRole,
        InPlaylistRole,  // "same" | "similar" | "" : déjà dans la playlist cible ?
        ExtraRole,       // texte complémentaire par ligne (ex. « 12 écoutes »)
    };

    TrackModel(const Paths *paths, QObject *parent = nullptr) : QAbstractListModel(parent), m_paths(paths) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_tracks.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_tracks.size()); }
    const QList<Track> &tracks() const { return m_tracks; }
    const Track *at(int i) const { return (i >= 0 && i < m_tracks.size()) ? &m_tracks[i] : nullptr; }
    void setTracks(const QList<Track> &tracks, const QStringList &extra = {});
    // Met à jour un morceau sans réinitialiser la liste (pas de saut de défilement).
    void updateTrack(const Track &t);
    // Morceaux de la playlist cible (ids, clés artiste|titre) à signaler dans la liste.
    void setMarks(const QSet<qint64> &ids, const QSet<QString> &keys);

signals:
    void countChanged();

private:
    QString cover(const Track &t) const;
    const Paths *m_paths;
    QString mark(const Track &t) const;
    QList<Track> m_tracks;
    QStringList m_extra;
    QSet<qint64> m_markIds;
    QSet<QString> m_markKeys;
};
