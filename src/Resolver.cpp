#include "Resolver.h"

void TrackResolver::resolve(const Track &input, Callback cb)
{
    if (input.id > 0) {
        if (auto t = m_db->track(input.id)) {
            cb(*t, {});
            return;
        }
    }
    if (!input.ytId.isEmpty()) {
        cb(m_db->upsertTrack(input), {});
        return;
    }
    if (input.title.isEmpty()) {
        cb(std::nullopt, QStringLiteral("morceau sans titre"));
        return;
    }
    // 1) Bibliothèque locale (id Spotify, clé artiste|titre, correspondances mémorisées).
    if (auto local = m_db->findLocalMatch(input.spotifyId, input.artist, input.title)) {
        Track merged = input;
        merged.ytId = local->ytId;
        cb(m_db->upsertTrack(merged), {});
        return;
    }
    // 2) Recherche YouTube avec score de correspondance.
    m_yt->findMatch(input.artist, input.title, input.duration, [db = m_db, input, cb](std::optional<Track> found, QString err) {
        if (!err.isEmpty() || !found) {
            cb(std::nullopt, err.isEmpty() ? QStringLiteral("aucune correspondance fiable sur YouTube pour « %1 — %2 »")
                                                 .arg(input.artist, input.title)
                                           : err);
            return;
        }
        db->rememberMatch(input.artist, input.title, found->ytId);
        Track merged = input;
        merged.ytId = found->ytId;
        if (merged.thumbnail.isEmpty())
            merged.thumbnail = found->thumbnail;
        if (merged.duration <= 0)
            merged.duration = found->duration;
        cb(db->upsertTrack(merged), {});
    });
}
