#pragma once

#include <functional>
#include <optional>

#include "Database.h"
#include "Track.h"
#include "YtDlp.h"

// Transforme une description de morceau (résultat Spotify, ligne d'import…) en morceau
// connu en base : bibliothèque locale d'abord, puis recherche YouTube avec score.
class TrackResolver {
public:
    using Callback = std::function<void(std::optional<Track>, QString error)>;

    TrackResolver(Database *db, YtDlp *yt) : m_db(db), m_yt(yt) {}
    // Le rappel peut être appelé immédiatement (morceau déjà connu) ou plus tard.
    void resolve(const Track &input, Callback cb);

private:
    Database *m_db;
    YtDlp *m_yt;
};
