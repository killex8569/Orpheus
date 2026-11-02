#include "TrackModel.h"

#include <QUrl>

#include "Matcher.h"

QString TrackModel::cover(const Track &t) const
{
    // Pochette locale si elle existe (hors ligne), sinon miniature distante.
    if (!t.ytId.isEmpty()) {
        const QString local = m_paths->coverFile(t.ytId);
        if (QFileInfo::exists(local))
            return QUrl::fromLocalFile(local).toString();
    }
    if (t.thumbnail.startsWith(QLatin1String("http")))
        return t.thumbnail;
    return t.ytId.isEmpty() ? QString() : thumbUrl(t.ytId);
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return {};
    const Track &t = m_tracks[index.row()];
    switch (role) {
    case TrackIdRole: return t.id;
    case YtIdRole: return t.ytId;
    case TitleRole: return t.title;
    case ArtistRole: return t.artist;
    case AlbumRole: return t.album;
    case DurationTextRole: return t.duration > 0 ? formatTime(t.duration) : QString();
    case CoverRole: return cover(t);
    case StateRole: return t.id > 0 ? t.state : QString();
    case QualityRole: return t.quality();
    case ErrorRole: return t.error;
    case InPlaylistRole: return mark(t);
    case ExtraRole: return m_extra.value(index.row());
    }
    return {};
}

QHash<int, QByteArray> TrackModel::roleNames() const
{
    return {{TrackIdRole, "trackId"}, {YtIdRole, "ytId"},       {TitleRole, "title"},
            {ArtistRole, "artist"},   {AlbumRole, "album"},     {DurationTextRole, "durationText"},
            {CoverRole, "cover"},     {StateRole, "trackState"}, {QualityRole, "quality"},
            {ErrorRole, "error"},     {InPlaylistRole, "inPlaylist"},
            {ExtraRole, "extra"}};
}

void TrackModel::setTracks(const QList<Track> &tracks, const QStringList &extra)
{
    beginResetModel();
    m_tracks = tracks;
    m_extra = extra;
    endResetModel();
    emit countChanged();
}

void TrackModel::updateTrack(const Track &t)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        // Un résultat de recherche n'a pas encore d'id local : on le reconnaît par son id YouTube.
        const bool same = (t.id > 0 && m_tracks[i].id == t.id) || (!t.ytId.isEmpty() && m_tracks[i].ytId == t.ytId);
        if (same) {
            m_tracks[i] = t;
            emit dataChanged(index(i), index(i));
        }
    }
}

QString TrackModel::mark(const Track &t) const
{
    if (m_markIds.isEmpty())
        return {};
    if (t.id > 0 && m_markIds.contains(t.id))
        return QStringLiteral("same");
    const QString key = Matcher::matchKey(t.artist, t.title);
    return !key.isEmpty() && m_markKeys.contains(key) ? QStringLiteral("similar") : QString();
}

void TrackModel::setMarks(const QSet<qint64> &ids, const QSet<QString> &keys)
{
    if (ids == m_markIds && keys == m_markKeys)
        return;
    m_markIds = ids;
    m_markKeys = keys;
    if (!m_tracks.isEmpty())
        emit dataChanged(index(0), index(int(m_tracks.size()) - 1), {InPlaylistRole});
}
