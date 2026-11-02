#pragma once

#include <QList>
#include <QPair>
#include <QString>

// Normalisation des titres et choix du meilleur résultat YouTube pour un morceau
// (import Spotify → YouTube).
namespace Matcher {

struct Candidate {
    QString title;
    QString channel;
    double duration = 0;
};

// Minuscules, sans accents, ponctuation remplacée par des espaces.
QString normalize(const QString &s);
// Retire parenthèses/crochets et mentions « feat. ».
QString stripDecorations(const QString &s);
// « Artiste - Titre » à partir d'un titre de vidéo YouTube.
QPair<QString, QString> splitYoutubeTitle(const QString &title, const QString &channel);
// Premier artiste d'une liste (« A, B », « A & B », « A feat. B »…), casse conservée.
QString primaryArtist(const QString &artist);
// Clé stable « artiste|titre » pour reconnaître un morceau quelle que soit sa source.
QString matchKey(const QString &artist, const QString &title);
// Score de 0 à ~1.2 : plus c'est haut, plus le candidat correspond.
double score(const QString &artist, const QString &title, double duration, const Candidate &c);

constexpr double AcceptThreshold = 0.55;
// Index du meilleur candidat acceptable, ou -1.
int best(const QString &artist, const QString &title, double duration, const QList<Candidate> &cands);

}  // namespace Matcher
