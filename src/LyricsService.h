#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <optional>

#include "Database.h"
#include "Lyrics.h"

// Paroles du morceau en cours, et récupération en arrière-plan de celles des morceaux
// gardés en local (écoute hors ligne), au rythme toléré par LRCLIB.
class LyricsService : public QObject {
    Q_OBJECT
public:
    explicit LyricsService(Database *db, QObject *parent = nullptr);

    // Morceau en cours ; `playerDuration` sert si la durée est inconnue en base.
    void setCurrent(const std::optional<Track> &track, double playerDuration);
    // Récupération en arrière-plan (ignorée si déjà en base).
    void enqueue(qint64 trackId);
    // Tous les morceaux en local encore sans paroles.
    void enqueueMissing();

    // "" | loading | synced | plain | instrumental | missing | offline
    QString status() const { return m_status; }
    QVariantList lines() const { return m_lines; }
    QString plain() const { return m_plain; }
    // Ligne en cours à `position` (−1 si pas de paroles synchronisées).
    int indexAt(double position) const;

signals:
    void changed();

private:
    void apply(const StoredLyrics &l);
    void fetch(const Track &t, std::function<void(bool reached)> cb);
    void pump();
    bool isKnown(qint64 trackId) const;

    Database *m_db;
    LyricsClient m_client;
    qint64 m_trackId = -1;
    QList<LyricLine> m_parsed;
    QVariantList m_lines;
    QString m_plain, m_status;
    QList<qint64> m_queue;
    bool m_busy = false;
    QHash<qint64, int> m_failures;  // échecs par morceau (cette session)
    int m_failStreak = 0;           // échecs d'affilée : sans doute hors ligne
};
