#include "LyricsService.h"

#include <QTimer>

namespace {
constexpr qint64 kRetryMissingSecs = 7 * 86400;  // « introuvable » redemandé après une semaine
}

LyricsService::LyricsService(Database *db, QObject *parent) : QObject(parent), m_db(db) {}

bool LyricsService::isKnown(qint64 trackId) const
{
    const auto stored = m_db->lyrics(trackId);
    return stored && (stored->status == "found" || nowSecs() - stored->fetchedAt < kRetryMissingSecs);
}

int LyricsService::indexAt(double position) const
{
    return m_status == QLatin1String("synced") ? lyricIndexAt(m_parsed, position) : -1;
}

void LyricsService::setCurrent(const std::optional<Track> &t, double playerDuration)
{
    const qint64 id = t ? t->id : 0;
    if (id == m_trackId)
        return;
    m_trackId = id;
    m_parsed.clear();
    m_lines.clear();
    m_plain.clear();
    m_status.clear();
    if (!t) {
        emit changed();
        return;
    }
    // Déjà en base : disponible immédiatement, même hors ligne.
    if (isKnown(id)) {
        apply(*m_db->lyrics(id));
        return;
    }
    m_status = QStringLiteral("loading");
    emit changed();
    Track query = *t;
    if (query.duration <= 0)
        query.duration = playerDuration;
    fetch(query, [this, id](bool reached) {
        if (id != m_trackId)
            return;  // l'utilisateur est passé à un autre morceau
        if (const auto stored = m_db->lyrics(id); reached && stored) {
            apply(*stored);
        } else {
            m_status = QStringLiteral("offline");
            emit changed();
            enqueue(id);  // nouvel essai automatique en arrière-plan
        }
    });
}

void LyricsService::apply(const StoredLyrics &l)
{
    m_parsed.clear();
    m_lines.clear();
    m_plain.clear();
    if (l.status != "found") {
        m_status = QStringLiteral("missing");
    } else if (l.instrumental) {
        m_status = QStringLiteral("instrumental");
    } else if (!l.synced.isEmpty() && !(m_parsed = parseLrc(l.synced)).isEmpty()) {
        m_status = QStringLiteral("synced");
        for (const LyricLine &line : std::as_const(m_parsed))
            m_lines.append(QVariantMap{{"time", line.time}, {"text", line.text}});
    } else if (!l.plain.trimmed().isEmpty()) {
        m_status = QStringLiteral("plain");
        m_plain = l.plain.trimmed();
    } else {
        m_status = QStringLiteral("missing");
    }
    emit changed();
}

void LyricsService::fetch(const Track &t, std::function<void(bool)> cb)
{
    m_client.fetch(t.artist, t.title, t.duration, [this, id = t.id, cb](LyricsClient::Result r) {
        // Hors ligne : rien n'est enregistré, on réessaiera plus tard.
        if (r.reached) {
            StoredLyrics l;
            l.status = r.found ? QStringLiteral("found") : QStringLiteral("missing");
            l.synced = r.synced;
            l.plain = r.plain;
            l.instrumental = r.instrumental;
            m_db->saveLyrics(id, l);
        }
        cb(r.reached);
    });
}

void LyricsService::enqueue(qint64 trackId)
{
    if (trackId <= 0 || m_queue.contains(trackId) || isKnown(trackId))
        return;
    m_queue.append(trackId);
    pump();
}

void LyricsService::enqueueMissing()
{
    for (const Track &t : m_db->tracksNeedingLyrics(1000))
        enqueue(t.id);
}

void LyricsService::pump()
{
    if (m_busy || m_queue.isEmpty())
        return;
    const auto t = m_db->track(m_queue.takeFirst());
    if (!t) {
        pump();
        return;
    }
    m_busy = true;
    fetch(*t, [this, id = t->id](bool reached) {
        m_busy = false;
        if (!reached) {
            // Réessayé plus tard (3 fois au plus par session), sans bloquer les suivants.
            if (++m_failures[id] < 3)
                m_queue.append(id);
            // Plusieurs échecs d'affilée : pas de réseau, pause de 5 minutes.
            const int wait = ++m_failStreak >= 3 ? 5 * 60 * 1000 : 5000;
            if (wait > 5000)
                m_failStreak = 0;
            QTimer::singleShot(wait, this, &LyricsService::pump);
            return;
        }
        m_failStreak = 0;
        // Le morceau en cours attendait peut-être ces paroles.
        if (id == m_trackId && m_status != "synced" && m_status != "plain")
            if (const auto stored = m_db->lyrics(id))
                apply(*stored);
        // Espacé, par politesse envers LRCLIB (service gratuit).
        QTimer::singleShot(1500, this, &LyricsService::pump);
    });
}
