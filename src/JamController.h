#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "Database.h"
#include "Jam.h"
#include "Player.h"
#include "TrackModel.h"

// Relie une session de Jam au lecteur local : charge le morceau commun (fichier local,
// sinon téléchargement), démarre à l'heure convenue, puis corrige la dérive en jouant
// très légèrement sur la vitesse (ou par un saut si l'écart est grand).
class JamController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool isHost READ isHost NOTIFY changed)
    Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QVariantList members READ members NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QVariantList discovered READ discovered NOTIFY discoveredChanged)
    Q_PROPERTY(TrackModel *queue READ queue CONSTANT)
    Q_PROPERTY(QString myName READ myName WRITE setMyName NOTIFY settingsChanged)
    Q_PROPERTY(int latencyMs READ latencyMs WRITE setLatencyMs NOTIFY settingsChanged)

public:
    JamController(Database *db, Player *player, const Paths *paths, QObject *parent = nullptr);

    bool active() const { return m_session.role() != JamSession::None; }
    bool isHost() const { return m_session.role() == JamSession::Host; }
    bool connecting() const { return m_session.connecting(); }
    QString name() const { return m_session.state().name; }
    QVariantList members() const;
    int currentIndex() const { return m_session.state().current() ? m_session.state().index : -1; }
    QString address() const;
    QString statusText() const { return m_status; }
    QVariantList discovered() const;
    TrackModel *queue() { return &m_queue; }
    QString myName() const;
    void setMyName(const QString &name);
    int latencyMs() const { return m_latencyMs; }
    void setLatencyMs(int ms);

    Q_INVOKABLE void create();
    // « 192.168.1.20 » ou « 192.168.1.20:47801 ».
    Q_INVOKABLE void join(const QString &address);
    Q_INVOKABLE void joinDiscovered(int index);
    Q_INVOKABLE void leave();
    Q_INVOKABLE void setDiscovering(bool on) { m_session.setDiscovering(on); }

    // Commandes relayées par Backend pendant un Jam (elles s'appliquent à tous).
    // Renvoie le nombre de morceaux envoyés.
    int add(const QList<Track> &tracks);
    void togglePlay();
    void next() { m_session.next(); }
    void prev() { m_session.prev(); }
    void seek(double position) { m_session.seek(position); }
    void jumpTo(int row);
    void removeAt(int row);

signals:
    void changed();
    void queueChanged();  // morceaux de la file commune ajoutés, retirés ou déplacés
    void statusChanged();
    void discoveredChanged();
    void settingsChanged();
    void toast(const QString &message, bool error);

private:
    void onRoleChanged();
    void onStateChanged();
    void tick();
    void setStatus(const QString &s);
    Track trackFor(const Jam::Item &item);

    JamSession m_session;
    Database *m_db;
    Player *m_player;
    TrackModel m_queue;
    QList<qint64> m_shownItems;
    QHash<QString, Track> m_tracks;  // ytId → morceau en base
    qint64 m_loadedItem = -1;        // élément du Jam demandé au lecteur
    qint64 m_loadAt = 0;
    qint64 m_readyItem = 0;          // élément déjà signalé « prêt » à l'hôte
    qint64 m_lastSeek = 0;
    qint64 m_endedTrack = 0;         // morceau arrivé à sa fin (en attente du suivant)
    int m_latencyMs = 0;
    bool m_wasActive = false;
    QString m_status;
    QTimer m_tick, m_startTimer;
};
