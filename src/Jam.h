#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <optional>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

// Jam : écoute partagée sur le réseau local.
//
// Seule la synchronisation circule sur le réseau, jamais l'audio : chacun lit le
// morceau depuis son disque (ou le télécharge). L'hôte fait autorité : les autres
// lui envoient leurs demandes (ajout, pause, saut…), il les applique puis diffuse
// l'état complet à tous. Messages JSON, un par ligne, sur TCP ; les Jams se
// signalent par diffusion UDP sur le réseau local.
namespace Jam {

constexpr quint16 DiscoveryPort = 47800;
constexpr quint16 SessionPort = 47801;
constexpr int ProtocolVersion = 2;  // 2 : code d'accès obligatoire
constexpr int MaxItems = 1000;
constexpr int MaxMembers = 32;
constexpr int MaxCodeAttempts = 3;  // au-delà, l'adresse IP est bloquée jusqu'à la fin du Jam

qint64 nowMs();
// Identifiant de vidéo YouTube strict : il sert à nommer des fichiers.
bool isValidYtId(const QString &id);
// Code d'accès : exactement 4 chiffres.
bool isValidCode(const QString &code);
// Preuve de connaissance du code, sans l'envoyer : HMAC-SHA256(code, défi de l'hôte).
QString codeProof(const QString &code, const QString &nonce);

struct Item {
    qint64 id = 0;  // attribué par l'hôte
    QString ytId, title, artist, thumbnail, addedBy;
    double duration = 0;
    QJsonObject toJson() const;
    // nullopt si l'entrée est invalide (données venues du réseau).
    static std::optional<Item> fromJson(const QJsonObject &o);
};

struct Member {
    int id = 0;
    QString name;
    bool ready = false;  // morceau en cours chargé et prêt à jouer
};

struct State {
    QString name;
    QList<Item> items;
    int index = 0;           // morceau en cours ; >= items.size() : file terminée
    bool playing = false;
    bool waiting = false;    // nouveau morceau : on attend que tout le monde soit prêt
    double anchorPos = 0;    // position (s) au moment anchorTime
    qint64 anchorTime = 0;   // horloge de l'hôte (ms)
    QList<Member> members;
    quint64 seq = 0;

    const Item *current() const { return index >= 0 && index < items.size() ? &items[index] : nullptr; }
    // Position attendue (s) à l'instant hostNow ; négative si le départ est programmé plus tard.
    double positionAt(qint64 hostNow) const;
    QJsonObject toJson() const;
    static State fromJson(const QJsonObject &o);
};

// Décalage entre l'horloge de l'hôte et l'horloge locale, estimé comme NTP : on garde
// la mesure dont l'aller-retour a été le plus court (la plus fiable).
class ClockSync {
public:
    void addSample(qint64 sentLocal, qint64 hostTime, qint64 receivedLocal);
    bool ready() const { return !m_samples.isEmpty(); }
    qint64 offset() const;
    qint64 rtt() const;
    void reset() { m_samples.clear(); }

private:
    struct Sample {
        qint64 rtt, offset;
    };
    QList<Sample> m_samples;
};

struct Discovered {
    QString id;  // identifiant du Jam (reçu une fois par interface réseau)
    QString name;
    QHostAddress address;
    quint16 port = 0;
    int members = 0;
    qint64 lastSeen = 0;
};

}  // namespace Jam

class JamSession : public QObject {
    Q_OBJECT
public:
    enum Role { None, Host, Client };

    explicit JamSession(QObject *parent = nullptr);
    ~JamSession() override;

    // Crée un Jam protégé par `code` (4 chiffres) ; `port` 0 = port libre au hasard.
    bool host(const QString &jamName, const QString &myName, const QString &code, quint16 port = Jam::SessionPort,
              QString *error = nullptr);
    void join(const QHostAddress &address, quint16 port, const QString &myName, const QString &code);
    void leave();

    Role role() const { return m_role; }
    bool connecting() const { return m_role == Client && m_myId == 0; }
    const Jam::State &state() const { return m_state; }
    int myId() const { return m_myId; }
    quint16 port() const;
    QString hostAddress() const { return m_hostAddress; }
    // Code d'accès (hôte uniquement).
    QString code() const { return m_role == Host ? m_code : QString(); }
    // Horloge de l'hôte estimée localement (ms).
    qint64 hostNow() const;
    qint64 clockRtt() const { return m_role == Host ? 0 : m_clock.rtt(); }
    // Adresses IPv4 locales (à communiquer aux amis pour une connexion manuelle).
    static QStringList localAddresses();

    // Demandes : appliquées par l'hôte puis diffusées à tous.
    void add(const QList<Jam::Item> &items);
    void remove(qint64 itemId);
    void play();
    void pause();
    void seek(double position);
    void next();
    void prev();
    void jump(qint64 itemId);
    void reportReady(qint64 itemId);
    // (Hôte) le lecteur de l'hôte a terminé ce morceau.
    void trackEnded(qint64 itemId);

    // Recherche des Jams du réseau local.
    void setDiscovering(bool on);
    const QList<Jam::Discovered> &discovered() const { return m_discovered; }

signals:
    void stateChanged();
    void roleChanged();
    void discoveredChanged();
    void notice(const QString &message, bool error);

private:
    struct Peer {
        QPointer<QTcpSocket> socket;
        int memberId = 0;
        QString name;
        QByteArray buffer;
        QString nonce;    // défi envoyé à la connexion
        quint32 ip = 0;   // adresse IPv4 du pair
    };

    // Hôte
    void onNewConnection();
    void rejectCode(QTcpSocket *s);
    void onPeerData(QTcpSocket *s);
    void dropPeer(QTcpSocket *s, const QString &reason = {});
    void handleRequest(int memberId, const QJsonObject &msg);
    void applyAdd(int memberId, const QJsonArray &items);
    void applyRemove(qint64 itemId);
    void applyPlay();
    void applyPause();
    void applySeek(double pos);
    void applyPrev();
    void applyReady(int memberId, qint64 itemId);
    void startItem(int index);
    void finishWaiting();
    void broadcastState();
    void broadcast(const QJsonObject &msg);
    void sendBeacon();
    void hostTick();
    QString memberName(int id) const;

    // Client
    void onClientData();
    void sendToHost(const QJsonObject &msg);
    void sendPing();
    void endSession(const QString &message, bool error);

    void request(const QJsonObject &msg);  // hôte : applique ; client : envoie
    void readDiscovery();

    Role m_role = None;
    Jam::State m_state;
    int m_myId = 0;
    // Hôte
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Peer> m_peers;
    int m_nextMemberId = 1;
    QString m_code;
    QHash<quint32, int> m_failures;  // mauvais codes par adresse IPv4
    QSet<quint32> m_banned;
    qint64 m_nextItemId = 1;
    QTimer m_beaconTimer, m_waitTimer, m_hostTick;
    // Client
    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
    Jam::ClockSync m_clock;
    QTimer m_pingTimer, m_connectTimer;
    int m_pingsSent = 0;
    QString m_hostAddress;
    QString m_joinName, m_joinCode;
    // Découverte
    QUdpSocket *m_discovery = nullptr;
    QTimer m_expireTimer;
    QList<Jam::Discovered> m_discovered;
    QString m_jamId;
};
