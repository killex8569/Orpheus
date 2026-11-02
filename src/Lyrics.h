#pragma once

#include <QJsonArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrlQuery>
#include <functional>

// Paroles via LRCLIB (lrclib.net) : base libre et gratuite, sans clé, avec des paroles
// synchronisées au format LRC. Elles sont ensuite gardées en base pour l'écoute hors ligne.
struct LyricLine {
    double time = 0;  // secondes
    QString text;
};

// « [mm:ss.xx] texte » → lignes triées (plusieurs horodatages par ligne acceptés,
// balises de métadonnées comme [ar:…] ignorées, [offset:±ms] appliqué).
QList<LyricLine> parseLrc(const QString &lrc);
// Index de la ligne en cours à `position` (−1 avant la première ligne).
int lyricIndexAt(const QList<LyricLine> &lines, double position);

class LyricsClient : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool reached = false;  // LRCLIB a répondu (sinon : hors ligne, réessayer plus tard)
        bool found = false;
        bool instrumental = false;
        QString synced, plain;
        QString error;
    };

    explicit LyricsClient(QObject *parent = nullptr);
    void fetch(const QString &artist, const QString &title, double duration, std::function<void(Result)> cb);

private:
    void search(const QUrlQuery &query, std::function<void(QJsonArray, bool reached, QString error)> cb, int attempt = 0);
    QNetworkAccessManager m_net;
};
