#include "Jam.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <algorithm>

namespace {

constexpr qsizetype kMaxLine = 1024 * 1024;  // au-delà : pair fautif, déconnecté
constexpr int kWaitTimeoutMs = 20000;        // attente max. des retardataires sur un nouveau morceau
constexpr int kStartLeadMs = 700;            // départ programmé un peu plus tard, pour tous en même temps
constexpr int kResumeLeadMs = 400;

QByteArray encode(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QString clip(const QString &s, int max)
{
    return s.simplified().left(max);
}

// Interfaces du réseau local réel : sans les réseaux virtuels (Docker, machines
// virtuelles, VPN…), où personne ne peut rejoindre le Jam.
QList<QNetworkInterface> lanInterfaces()
{
    static const QStringList virtualPrefixes{"docker", "br-",  "virbr",     "veth", "vmnet", "vboxnet", "tun",
                                             "tap",    "wg",   "tailscale", "zt",   "lxc",   "lxd",     "podman",
                                             "cni",    "flannel", "kube"};
    QList<QNetworkInterface> real, all;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        all << iface;
        const QString n = iface.name();
        if (iface.type() != QNetworkInterface::Virtual
            && std::none_of(virtualPrefixes.cbegin(), virtualPrefixes.cend(), [&n](const QString &p) { return n.startsWith(p); }))
            real << iface;
    }
    // Wi-Fi d'abord : c'est le cas d'usage principal.
    std::stable_sort(real.begin(), real.end(), [](const QNetworkInterface &a, const QNetworkInterface &b) {
        return (a.type() == QNetworkInterface::Wifi) > (b.type() == QNetworkInterface::Wifi);
    });
    return real.isEmpty() ? all : real;
}

// Extrait les lignes complètes du tampon ; false si le tampon déborde.
bool takeLines(QByteArray &buffer, QList<QJsonObject> &out)
{
    qsizetype nl;
    while ((nl = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(nl);
        buffer.remove(0, nl + 1);
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            out.append(doc.object());
    }
    return buffer.size() <= kMaxLine;
}

}  // namespace

// ---------- Types ----------

namespace Jam {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool isValidCode(const QString &code)
{
    static const QRegularExpression re(QStringLiteral("^[0-9]{4}$"));
    return re.match(code).hasMatch();
}

QString codeProof(const QString &code, const QString &nonce)
{
    return QString::fromLatin1(
        QMessageAuthenticationCode::hash(nonce.toUtf8(), code.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool isValidYtId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return re.match(id).hasMatch();
}

QJsonObject Item::toJson() const
{
    return {{"id", id},       {"yt", ytId},         {"title", title}, {"artist", artist},
            {"thumb", thumbnail}, {"by", addedBy}, {"duration", duration}};
}

std::optional<Item> Item::fromJson(const QJsonObject &o)
{
    Item it;
    it.ytId = o.value("yt").toString();
    if (!isValidYtId(it.ytId))
        return std::nullopt;
    it.id = o.value("id").toInteger();
    it.title = clip(o.value("title").toString(), 300);
    it.artist = clip(o.value("artist").toString(), 300);
    it.addedBy = clip(o.value("by").toString(), 40);
    const QString thumb = o.value("thumb").toString();
    it.thumbnail = thumb.startsWith("https://") ? thumb.left(1000) : QString();
    it.duration = qBound(0.0, o.value("duration").toDouble(), 86400.0);
    if (it.title.isEmpty())
        it.title = it.ytId;
    return it;
}

double State::positionAt(qint64 hostNow) const
{
    if (waiting)
        return 0;
    if (!playing)
        return anchorPos;
    return anchorPos + double(hostNow - anchorTime) / 1000.0;
}

QJsonObject State::toJson() const
{
    QJsonArray its, mems;
    for (const Item &i : items)
        its.append(i.toJson());
    for (const Member &m : members)
        mems.append(QJsonObject{{"id", m.id}, {"name", m.name}, {"ready", m.ready}});
    return {{"name", name},           {"items", its},     {"index", index},
            {"playing", playing},     {"waiting", waiting}, {"anchorPos", anchorPos},
            {"anchorTime", anchorTime}, {"members", mems},  {"seq", qint64(seq)}};
}

State State::fromJson(const QJsonObject &o)
{
    State s;
    s.name = clip(o.value("name").toString(), 80);
    for (const QJsonValue &v : o.value("items").toArray()) {
        if (s.items.size() >= MaxItems)
            break;
        if (const auto it = Item::fromJson(v.toObject()))
            s.items.append(*it);
    }
    s.index = qBound(0, o.value("index").toInt(), int(s.items.size()));
    s.playing = o.value("playing").toBool();
    s.waiting = o.value("waiting").toBool();
    s.anchorPos = qMax(0.0, o.value("anchorPos").toDouble());
    s.anchorTime = o.value("anchorTime").toInteger();
    for (const QJsonValue &v : o.value("members").toArray()) {
        if (s.members.size() >= MaxMembers)
            break;
        const QJsonObject m = v.toObject();
        s.members.append({m.value("id").toInt(), clip(m.value("name").toString(), 40), m.value("ready").toBool()});
    }
    s.seq = quint64(o.value("seq").toInteger());
    return s;
}

void ClockSync::addSample(qint64 sentLocal, qint64 hostTime, qint64 receivedLocal)
{
    const qint64 rtt = receivedLocal - sentLocal;
    if (rtt < 0 || rtt > 5000)
        return;
    m_samples.append({rtt, hostTime - (sentLocal + receivedLocal) / 2});
    if (m_samples.size() > 8)
        m_samples.removeFirst();
}

qint64 ClockSync::offset() const
{
    if (m_samples.isEmpty())
        return 0;
    return std::min_element(m_samples.cbegin(), m_samples.cend(), [](auto &a, auto &b) { return a.rtt < b.rtt; })->offset;
}

qint64 ClockSync::rtt() const
{
    if (m_samples.isEmpty())
        return 0;
    return std::min_element(m_samples.cbegin(), m_samples.cend(), [](auto &a, auto &b) { return a.rtt < b.rtt; })->rtt;
}

}  // namespace Jam

using namespace Jam;

// ---------- Session ----------

JamSession::JamSession(QObject *parent) : QObject(parent)
{
    m_beaconTimer.setInterval(2000);
    connect(&m_beaconTimer, &QTimer::timeout, this, &JamSession::sendBeacon);
    m_waitTimer.setSingleShot(true);
    connect(&m_waitTimer, &QTimer::timeout, this, &JamSession::finishWaiting);
    m_hostTick.setInterval(1000);
    connect(&m_hostTick, &QTimer::timeout, this, &JamSession::hostTick);
    connect(&m_pingTimer, &QTimer::timeout, this, &JamSession::sendPing);
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        if (connecting())
            endSession(QStringLiteral("Impossible de joindre le Jam (délai dépassé)"), true);
    });
    m_expireTimer.setInterval(2000);
    connect(&m_expireTimer, &QTimer::timeout, this, [this] {
        const qint64 limit = nowMs() - 7000;
        const auto n = m_discovered.removeIf([limit](const Discovered &d) { return d.lastSeen < limit; });
        if (n)
            emit discoveredChanged();
    });
}

JamSession::~JamSession()
{
    if (m_role != None)
        leave();
}

quint16 JamSession::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

qint64 JamSession::hostNow() const
{
    return m_role == Client ? nowMs() + m_clock.offset() : nowMs();
}

QStringList JamSession::localAddresses()
{
    QStringList out;
    for (const QNetworkInterface &iface : lanInterfaces()) {
        for (const QNetworkAddressEntry &e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out << e.ip().toString();
    }
    return out;
}

void JamSession::request(const QJsonObject &msg)
{
    if (m_role == Host)
        handleRequest(m_myId, msg);
    else if (m_role == Client && !connecting())
        sendToHost(msg);
}

void JamSession::add(const QList<Item> &items)
{
    QJsonArray arr;
    for (const Item &i : items)
        arr.append(i.toJson());
    request({{"type", "add"}, {"items", arr}});
}

void JamSession::remove(qint64 itemId) { request({{"type", "remove"}, {"id", itemId}}); }
void JamSession::play() { request({{"type", "play"}}); }
void JamSession::pause() { request({{"type", "pause"}}); }
void JamSession::seek(double position) { request({{"type", "seek"}, {"pos", position}}); }
void JamSession::next() { request({{"type", "next"}}); }
void JamSession::prev() { request({{"type", "prev"}}); }
void JamSession::jump(qint64 itemId) { request({{"type", "jump"}, {"id", itemId}}); }
void JamSession::reportReady(qint64 itemId) { request({{"type", "ready"}, {"id", itemId}}); }

void JamSession::trackEnded(qint64 itemId)
{
    if (m_role == Host && m_state.current() && m_state.current()->id == itemId && !m_state.waiting)
        startItem(m_state.index + 1);
}

// ---------- Hôte ----------

bool JamSession::host(const QString &jamName, const QString &myName, const QString &code, quint16 port, QString *error)
{
    if (!isValidCode(code)) {
        if (error)
            *error = QStringLiteral("le code doit faire exactement 4 chiffres");
        return false;
    }
    if (m_role != None)
        leave();
    m_server = new QTcpServer(this);
    // Port habituel (connexion manuelle plus simple), sinon n'importe lequel.
    if (!m_server->listen(QHostAddress::AnyIPv4, port) && !m_server->listen(QHostAddress::AnyIPv4, 0)) {
        if (error)
            *error = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &JamSession::onNewConnection);
    m_role = Host;
    m_code = code;
    m_failures.clear();
    m_banned.clear();
    m_myId = 1;
    m_nextMemberId = 2;
    m_nextItemId = 1;
    m_jamId = QString::number(QRandomGenerator::global()->generate64(), 16);
    m_state = State();
    m_state.name = clip(jamName, 80);
    m_state.members.append({m_myId, clip(myName, 40), false});
    m_beaconTimer.start();
    m_hostTick.start();
    sendBeacon();
    emit roleChanged();
    broadcastState();
    return true;
}

void JamSession::onNewConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        const quint32 ip = s->peerAddress().toIPv4Address();
        // Adresse bloquée (trop de mauvais codes) : coupée sans la moindre réponse.
        if (m_banned.contains(ip)) {
            s->abort();
            s->deleteLater();
            continue;
        }
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        Peer peer;
        peer.socket = s;
        peer.ip = ip;
        peer.nonce = QString::number(QRandomGenerator::system()->generate64(), 16)
                     + QString::number(QRandomGenerator::system()->generate64(), 16);
        m_peers.insert(s, peer);
        // Défi : le client prouve qu'il connaît le code sans l'envoyer.
        s->write(encode({{"type", "challenge"}, {"v", ProtocolVersion}, {"nonce", peer.nonce}}));
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onPeerData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] { dropPeer(s); });
        // Pas de présentation dans les 5 s : ce n'est pas un client Orpheus.
        QTimer::singleShot(5000, s, [this, s] {
            if (m_peers.contains(s) && m_peers[s].memberId == 0)
                dropPeer(s);
        });
    }
}

void JamSession::onPeerData(QTcpSocket *s)
{
    if (!m_peers.contains(s))
        return;
    m_peers[s].buffer += s->readAll();
    QList<QJsonObject> msgs;
    if (!takeLines(m_peers[s].buffer, msgs)) {
        dropPeer(s);
        return;
    }
    for (const QJsonObject &msg : std::as_const(msgs)) {
        if (!m_peers.contains(s))
            return;
        Peer &peer = m_peers[s];
        const QString type = msg.value("type").toString();
        if (type == "ping" && peer.memberId > 0) {
            s->write(encode({{"type", "pong"}, {"t", msg.value("t")}, {"h", nowMs()}}));
        } else if (type == "hello" && peer.memberId == 0) {
            if (msg.value("v").toInt() != ProtocolVersion) {
                s->write(encode({{"type", "error"}, {"text", "Version d'Orpheus différente de celle de l'hôte : mettez-la à jour."}}));
                s->disconnectFromHost();
                return;
            }
            if (msg.value("proof").toString() != codeProof(m_code, peer.nonce)) {
                rejectCode(s);
                return;
            }
            m_failures.remove(peer.ip);
            if (m_state.members.size() >= MaxMembers) {
                s->write(encode({{"type", "error"}, {"text", "Ce Jam est complet."}}));
                s->disconnectFromHost();
                return;
            }
            peer.memberId = m_nextMemberId++;
            peer.name = clip(msg.value("name").toString(), 40);
            if (peer.name.isEmpty())
                peer.name = QStringLiteral("Invité %1").arg(peer.memberId);
            m_state.members.append({peer.memberId, peer.name, false});
            s->write(encode({{"type", "welcome"}, {"you", peer.memberId}}));
            const QString text = QStringLiteral("%1 a rejoint le Jam").arg(peer.name);
            broadcast({{"type", "notice"}, {"text", text}});
            emit notice(text, false);
            broadcastState();
        } else if (peer.memberId > 0) {
            handleRequest(peer.memberId, msg);
        }
    }
}

void JamSession::rejectCode(QTcpSocket *s)
{
    const quint32 ip = m_peers.value(s).ip;
    const int failures = ++m_failures[ip];
    if (failures >= MaxCodeAttempts) {
        m_banned.insert(ip);
        s->write(encode({{"type", "error"}, {"text", "Trop de codes incorrects : ce Jam ne répondra plus à cet appareil."}}));
        // Autres connexions en cours depuis la même adresse : coupées sans réponse.
        QList<QTcpSocket *> others;
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
            if (it.key() != s && it->ip == ip && it->memberId == 0)
                others << it.key();
        for (QTcpSocket *o : std::as_const(others)) {
            m_peers.remove(o);
            o->disconnect(this);
            o->abort();
            o->deleteLater();
        }
    } else {
        s->write(encode({{"type", "error"},
                         {"text", QStringLiteral("Code incorrect (essai %1 sur %2)").arg(failures).arg(MaxCodeAttempts)}}));
    }
    s->disconnectFromHost();
}

void JamSession::dropPeer(QTcpSocket *s, const QString &reason)
{
    const auto it = m_peers.find(s);
    if (it == m_peers.end())
        return;
    const Peer peer = *it;
    m_peers.erase(it);
    s->disconnect(this);
    if (!reason.isEmpty())
        s->write(encode({{"type", "error"}, {"text", reason}}));
    s->disconnectFromHost();
    s->deleteLater();
    if (peer.memberId == 0 || m_role != Host)
        return;
    m_state.members.removeIf([&peer](const Member &m) { return m.id == peer.memberId; });
    const QString text = QStringLiteral("%1 a quitté le Jam").arg(peer.name);
    broadcast({{"type", "notice"}, {"text", text}});
    emit notice(text, false);
    // Personne d'autre à attendre ?
    if (m_state.waiting && std::all_of(m_state.members.cbegin(), m_state.members.cend(), [](const Member &m) { return m.ready; }))
        finishWaiting();
    else
        broadcastState();
}

void JamSession::handleRequest(int memberId, const QJsonObject &msg)
{
    const QString type = msg.value("type").toString();
    if (type == "add")
        applyAdd(memberId, msg.value("items").toArray());
    else if (type == "remove")
        applyRemove(msg.value("id").toInteger());
    else if (type == "play")
        applyPlay();
    else if (type == "pause")
        applyPause();
    else if (type == "seek")
        applySeek(msg.value("pos").toDouble());
    else if (type == "next")
        startItem(m_state.index + 1);
    else if (type == "prev")
        applyPrev();
    else if (type == "jump") {
        const qint64 id = msg.value("id").toInteger();
        for (int i = 0; i < m_state.items.size(); ++i)
            if (m_state.items[i].id == id) {
                startItem(i);
                break;
            }
    } else if (type == "ready")
        applyReady(memberId, msg.value("id").toInteger());
}

QString JamSession::memberName(int id) const
{
    for (const Member &m : m_state.members)
        if (m.id == id)
            return m.name;
    return {};
}

void JamSession::applyAdd(int memberId, const QJsonArray &arr)
{
    const bool finished = !m_state.current();
    QList<Item> added;
    for (const QJsonValue &v : arr) {
        if (m_state.items.size() >= MaxItems)
            break;
        auto it = Item::fromJson(v.toObject());
        if (!it)
            continue;
        it->id = m_nextItemId++;
        it->addedBy = memberName(memberId);
        m_state.items.append(*it);
        added.append(*it);
    }
    if (added.isEmpty())
        return;
    // Prévient les autres (l'auteur de l'ajout a déjà sa propre confirmation).
    const QString who = memberName(memberId);
    const QString text = added.size() == 1 ? QStringLiteral("%1 a ajouté « %2 »").arg(who, added.first().title)
                                           : QStringLiteral("%1 a ajouté %2 morceaux").arg(who).arg(added.size());
    broadcast({{"type", "notice"}, {"text", text}, {"from", memberId}});
    if (memberId != m_myId)
        emit notice(text, false);
    // File vide ou terminée : le premier ajout démarre aussitôt.
    if (finished)
        startItem(m_state.index);
    else
        broadcastState();
}

void JamSession::applyRemove(qint64 itemId)
{
    for (int i = 0; i < m_state.items.size(); ++i) {
        if (m_state.items[i].id != itemId)
            continue;
        m_state.items.removeAt(i);
        if (i < m_state.index) {
            --m_state.index;
            broadcastState();
        } else if (i == m_state.index) {
            startItem(m_state.index);  // le suivant prend sa place
        } else {
            broadcastState();
        }
        return;
    }
}

void JamSession::applyPlay()
{
    if (!m_state.current() || m_state.playing)
        return;
    m_state.playing = true;
    if (!m_state.waiting)
        m_state.anchorTime = nowMs() + kResumeLeadMs;
    broadcastState();
}

void JamSession::applyPause()
{
    if (!m_state.playing)
        return;
    if (!m_state.waiting)
        m_state.anchorPos = qMax(0.0, m_state.positionAt(nowMs()));
    m_state.playing = false;
    broadcastState();
}

void JamSession::applySeek(double pos)
{
    const Item *cur = m_state.current();
    if (!cur || m_state.waiting)
        return;
    pos = qMax(0.0, pos);
    if (cur->duration > 0)
        pos = qMin(pos, cur->duration);
    m_state.anchorPos = pos;
    m_state.anchorTime = nowMs() + (m_state.playing ? 250 : 0);
    broadcastState();
}

void JamSession::applyPrev()
{
    if (!m_state.waiting && m_state.current() && m_state.positionAt(nowMs()) > 3)
        applySeek(0);
    else
        startItem(qMax(0, m_state.index - 1));
}

void JamSession::applyReady(int memberId, qint64 itemId)
{
    const Item *cur = m_state.current();
    if (!cur || cur->id != itemId)
        return;
    bool all = true;
    for (Member &m : m_state.members) {
        if (m.id == memberId)
            m.ready = true;
        all = all && m.ready;
    }
    if (m_state.waiting && all)
        finishWaiting();
    else
        broadcastState();
}

void JamSession::startItem(int index)
{
    m_state.index = qBound(0, index, int(m_state.items.size()));
    for (Member &m : m_state.members)
        m.ready = false;
    m_state.anchorPos = 0;
    m_state.anchorTime = nowMs();
    m_waitTimer.stop();
    if (m_state.current()) {
        // Chacun charge (ou télécharge) le morceau ; départ commun quand tous sont prêts.
        m_state.waiting = true;
        m_state.playing = true;
        m_waitTimer.start(kWaitTimeoutMs);
    } else {
        m_state.waiting = false;
        m_state.playing = false;
    }
    broadcastState();
}

void JamSession::finishWaiting()
{
    if (m_role != Host || !m_state.waiting)
        return;
    m_waitTimer.stop();
    m_state.waiting = false;
    m_state.anchorPos = 0;
    m_state.anchorTime = nowMs() + kStartLeadMs;
    broadcastState();
}

void JamSession::hostTick()
{
    // Filet de sécurité si le lecteur de l'hôte ne signale pas la fin du morceau.
    const Item *cur = m_state.current();
    if (cur && m_state.playing && !m_state.waiting && cur->duration > 0 && m_state.positionAt(nowMs()) > cur->duration + 3)
        startItem(m_state.index + 1);
}

void JamSession::broadcast(const QJsonObject &msg)
{
    const QByteArray data = encode(msg);
    for (const Peer &p : std::as_const(m_peers))
        if (p.memberId > 0 && p.socket)
            p.socket->write(data);
}

void JamSession::broadcastState()
{
    ++m_state.seq;
    QJsonObject msg = m_state.toJson();
    msg.insert("type", "state");
    broadcast(msg);
    emit stateChanged();
}

void JamSession::sendBeacon()
{
    if (m_role != Host || !m_server)
        return;
    if (!m_discovery) {
        m_discovery = new QUdpSocket(this);
        connect(m_discovery, &QUdpSocket::readyRead, this, &JamSession::readDiscovery);
    }
    const QByteArray data = QJsonDocument(QJsonObject{{"orpheusJam", ProtocolVersion},
                                                      {"id", m_jamId},
                                                      {"name", m_state.name},
                                                      {"port", int(m_server->serverPort())},
                                                      {"members", int(m_state.members.size())}})
                                .toJson(QJsonDocument::Compact);
    // Adresse de diffusion de chaque interface (plus fiable que 255.255.255.255 seule).
    QList<QHostAddress> targets{QHostAddress::Broadcast};
    for (const QNetworkInterface &iface : lanInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::CanBroadcast))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.broadcast().isNull() && !targets.contains(e.broadcast()))
                targets << e.broadcast();
    }
    for (const QHostAddress &t : targets)
        m_discovery->writeDatagram(data, t, DiscoveryPort);
}

void JamSession::leave()
{
    if (m_role == Host) {
        broadcast({{"type", "end"}});
        for (auto it = m_peers.begin(); it != m_peers.end(); ++it)
            if (it->socket) {
                it->socket->disconnect(this);
                it->socket->flush();
                it->socket->disconnectFromHost();
                it->socket->deleteLater();
            }
        m_peers.clear();
        delete m_server;
        m_server = nullptr;
        m_beaconTimer.stop();
        m_waitTimer.stop();
        m_hostTick.stop();
        // Socket des annonces : fermé avec le Jam (sauf s'il sert à chercher des Jams).
        if (m_discovery && !m_expireTimer.isActive()) {
            delete m_discovery;
            m_discovery = nullptr;
        }
        m_role = None;
        m_myId = 0;
        m_code.clear();
        m_failures.clear();
        m_banned.clear();
        m_state = State();
        emit roleChanged();
        emit stateChanged();
    } else if (m_role == Client) {
        endSession({}, false);
    }
}

// ---------- Client ----------

void JamSession::join(const QHostAddress &address, quint16 port, const QString &myName, const QString &code)
{
    if (m_role != None)
        leave();
    m_role = Client;
    m_myId = 0;
    m_state = State();
    m_clock.reset();
    m_buffer.clear();
    m_pingsSent = 0;
    m_hostAddress = address.toString();
    m_joinName = clip(myName, 40);
    m_joinCode = code;
    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(m_socket, &QTcpSocket::readyRead, this, &JamSession::onClientData);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        endSession(connecting() ? QStringLiteral("Connexion refusée par l'hôte") : QStringLiteral("Connexion au Jam perdue"), true);
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        if (e != QAbstractSocket::RemoteHostClosedError)
            endSession(QStringLiteral("Impossible de joindre le Jam : %1").arg(m_socket ? m_socket->errorString() : QString()), true);
    });
    m_connectTimer.start(6000);
    emit roleChanged();
    emit stateChanged();
    m_socket->connectToHost(address, port);
}

void JamSession::sendToHost(const QJsonObject &msg)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(encode(msg));
}

void JamSession::sendPing()
{
    sendToHost({{"type", "ping"}, {"t", nowMs()}});
    if (++m_pingsSent == 8)
        m_pingTimer.setInterval(2000);
}

void JamSession::onClientData()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    QList<QJsonObject> msgs;
    if (!takeLines(m_buffer, msgs)) {
        endSession(QStringLiteral("Données invalides reçues de l'hôte"), true);
        return;
    }
    for (const QJsonObject &msg : std::as_const(msgs)) {
        const QString type = msg.value("type").toString();
        if (type == "pong") {
            m_clock.addSample(msg.value("t").toInteger(), msg.value("h").toInteger(), nowMs());
        } else if (type == "challenge") {
            if (msg.value("v").toInt() != ProtocolVersion) {
                endSession(QStringLiteral("Version d'Orpheus différente de celle de l'hôte : mettez-la à jour."), true);
                return;
            }
            sendToHost({{"type", "hello"},
                        {"v", ProtocolVersion},
                        {"name", m_joinName},
                        {"proof", codeProof(m_joinCode, msg.value("nonce").toString())}});
        } else if (type == "welcome") {
            m_myId = msg.value("you").toInt();
            m_connectTimer.stop();
            // Synchronisation d'horloge : quelques mesures rapprochées, puis régulières.
            m_pingsSent = 0;
            sendPing();
            m_pingTimer.start(250);
            emit roleChanged();
        } else if (type == "state") {
            m_state = State::fromJson(msg);
            emit stateChanged();
        } else if (type == "notice") {
            if (msg.value("from").toInt() != m_myId)
                emit notice(clip(msg.value("text").toString(), 300), false);
        } else if (type == "end") {
            endSession(QStringLiteral("L'hôte a terminé le Jam"), false);
            return;
        } else if (type == "error") {
            endSession(clip(msg.value("text").toString(), 300), true);
            return;
        }
        if (!m_socket)
            return;
    }
}

void JamSession::endSession(const QString &message, bool error)
{
    if (m_role != Client)
        return;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_pingTimer.stop();
    m_connectTimer.stop();
    m_role = None;
    m_myId = 0;
    m_state = State();
    emit roleChanged();
    emit stateChanged();
    if (!message.isEmpty())
        emit notice(message, error);
}

// ---------- Découverte ----------

void JamSession::setDiscovering(bool on)
{
    if (on && !m_expireTimer.isActive()) {
        if (!m_discovery) {
            m_discovery = new QUdpSocket(this);
            connect(m_discovery, &QUdpSocket::readyRead, this, &JamSession::readDiscovery);
        }
        if (m_discovery->state() != QAbstractSocket::BoundState)
            m_discovery->bind(QHostAddress::AnyIPv4, DiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        m_expireTimer.start();
    } else if (!on && m_expireTimer.isActive()) {
        m_expireTimer.stop();
        if (m_discovery && m_role != Host) {
            delete m_discovery;
            m_discovery = nullptr;
        }
        m_discovered.clear();
        emit discoveredChanged();
    }
}

void JamSession::readDiscovery()
{
    while (m_discovery && m_discovery->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_discovery->receiveDatagram(4096);
        if (!m_expireTimer.isActive())
            continue;  // socket ouvert pour émettre seulement (hôte)
        const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        const int port = o.value("port").toInt();
        if (o.value("orpheusJam").toInt() != ProtocolVersion || port <= 0 || port > 65535)
            continue;
        bool ok = false;
        const QHostAddress addr(dg.senderAddress().toIPv4Address(&ok));
        if (!ok)
            continue;
        Discovered d{clip(o.value("id").toString(), 32), clip(o.value("name").toString(), 80), addr, quint16(port),
                     o.value("members").toInt(), nowMs()};
        // Même Jam reçu par plusieurs chemins : une seule entrée, première adresse conservée.
        bool known = false;
        for (Discovered &e : m_discovered)
            if (d.id.isEmpty() ? (e.address == d.address && e.port == d.port) : e.id == d.id) {
                d.address = e.address;
                d.port = e.port;
                e = d;
                known = true;
            }
        if (!known)
            m_discovered.append(d);
        emit discoveredChanged();
    }
}
