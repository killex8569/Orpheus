#include "JamController.h"

#include <QDebug>

namespace {
// Écarts tolérés avant un saut (au-dessous : rattrapage par la vitesse).
constexpr double kSeekThresholdLocal = 0.35;
constexpr double kSeekThresholdStream = 1.5;
constexpr double kSpeedDeadZone = 0.03;
}  // namespace

JamController::JamController(Database *db, Player *player, const Paths *paths, QObject *parent)
    : QObject(parent), m_db(db), m_player(player), m_queue(paths)
{
    m_latencyMs = m_db->setting("jam_latency_ms", "0").toInt();
    connect(&m_session, &JamSession::roleChanged, this, &JamController::onRoleChanged);
    connect(&m_session, &JamSession::stateChanged, this, &JamController::onStateChanged);
    connect(&m_session, &JamSession::discoveredChanged, this, &JamController::discoveredChanged);
    connect(&m_session, &JamSession::notice, this, &JamController::toast);
    connect(m_player, &Player::endReached, this, [this](qint64 trackId) {
        m_endedTrack = trackId;  // fin normale : ne pas le recharger en attendant le suivant
        const Jam::Item *cur = m_session.state().current();
        if (isHost() && cur && m_tracks.value(cur->ytId).id == trackId)
            m_session.trackEnded(cur->id);
    });
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, &JamController::tick);
    m_startTimer.setSingleShot(true);
    m_startTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        m_player->setPaused(false);
        tick();
    });
}

// ---------- Propriétés ----------

QString JamController::myName() const
{
    QString def = qEnvironmentVariable("USER");
    if (!def.isEmpty())
        def[0] = def[0].toUpper();
    return m_db->setting("jam_name", def);
}

void JamController::setMyName(const QString &name)
{
    m_db->setSetting("jam_name", name.simplified().left(40));
    emit settingsChanged();
}

void JamController::setLatencyMs(int ms)
{
    m_latencyMs = qBound(-1000, ms, 1000);
    m_db->setSetting("jam_latency_ms", QString::number(m_latencyMs));
    emit settingsChanged();
}

QVariantList JamController::members() const
{
    QVariantList out;
    for (const Jam::Member &m : m_session.state().members)
        out.append(QVariantMap{{"name", m.name}, {"ready", m.ready}, {"me", m.id == m_session.myId()}, {"host", m.id == 1}});
    return out;
}

QString JamController::address() const
{
    if (isHost()) {
        const QStringList ips = JamSession::localAddresses();
        const QString port = m_session.port() == Jam::SessionPort ? QString() : ":" + QString::number(m_session.port());
        QStringList withPort;
        for (const QString &ip : ips)
            withPort << ip + port;
        return withPort.join(QStringLiteral(" · "));
    }
    return m_session.hostAddress();
}

QVariantList JamController::discovered() const
{
    QVariantList out;
    for (const Jam::Discovered &d : m_session.discovered())
        out.append(QVariantMap{{"name", d.name}, {"address", d.address.toString()}, {"members", d.members}});
    return out;
}

// ---------- Session ----------

void JamController::create()
{
    const QString me = myName().isEmpty() ? QStringLiteral("Hôte") : myName();
    QString err;
    if (!m_session.host(QStringLiteral("Jam de %1").arg(me), me, Jam::SessionPort, &err))
        emit toast(QStringLiteral("Impossible de créer le Jam : %1").arg(err), true);
    else
        emit toast(QStringLiteral("Jam créé : vos amis sur le même réseau peuvent le rejoindre"), false);
}

void JamController::join(const QString &text)
{
    QString host = text.trimmed();
    quint16 port = Jam::SessionPort;
    if (const int colon = host.lastIndexOf(':'); colon > 0 && host.count(':') == 1) {
        bool ok = false;
        const int p = host.mid(colon + 1).toInt(&ok);
        if (!ok || p <= 0 || p > 65535) {
            emit toast(QStringLiteral("Port invalide"), true);
            return;
        }
        port = quint16(p);
        host = host.left(colon);
    }
    const QHostAddress addr(host);
    if (addr.isNull()) {
        emit toast(QStringLiteral("Adresse invalide : saisissez l'adresse IP affichée chez l'hôte (ex. 192.168.1.20)"), true);
        return;
    }
    m_session.join(addr, port, myName());
}

void JamController::joinDiscovered(int index)
{
    const auto &list = m_session.discovered();
    if (index >= 0 && index < list.size())
        m_session.join(list[index].address, list[index].port, myName());
}

void JamController::leave()
{
    m_session.leave();
}

void JamController::onRoleChanged()
{
    const bool nowActive = active();
    if (nowActive && !m_wasActive) {
        // Le Jam prend la main sur le lecteur : sa file remplace la file locale.
        m_player->setExternalControl(true);
        m_loadedItem = -1;
        m_readyItem = 0;
        m_tick.start();
    } else if (!nowActive && m_wasActive) {
        m_tick.stop();
        m_startTimer.stop();
        m_player->setPaused(true);
        m_player->setExternalControl(false);
        m_queue.setTracks({});
        m_shownItems.clear();
        emit queueChanged();
        m_loadedItem = -1;
        setStatus({});
    }
    m_wasActive = nowActive;
    emit changed();
}

Track JamController::trackFor(const Jam::Item &item)
{
    auto it = m_tracks.find(item.ytId);
    if (it == m_tracks.end()) {
        Track t;
        t.ytId = item.ytId;
        t.title = item.title;
        t.artist = item.artist;
        t.duration = item.duration;
        t.thumbnail = item.thumbnail;
        it = m_tracks.insert(item.ytId, m_db->upsertTrack(t));
    }
    return *it;
}

void JamController::onStateChanged()
{
    if (!active()) {
        emit changed();
        return;
    }
    const Jam::State &st = m_session.state();
    QList<Track> tracks;
    QStringList extra;
    QList<qint64> ids;
    for (const Jam::Item &item : st.items) {
        tracks.append(trackFor(item));
        extra.append(item.addedBy.isEmpty() ? QString() : QStringLiteral("ajouté par %1").arg(item.addedBy));
        ids.append(item.id);
    }
    const bool itemsChanged = ids != m_shownItems;
    if (itemsChanged) {
        m_queue.setTracks(tracks, extra);
        m_shownItems = ids;
        emit queueChanged();
    }
    // Lecteur : nouveau morceau à charger, ou file modifiée (sans recharger le morceau en cours).
    const Jam::Item *cur = st.current();
    const qint64 curId = cur ? cur->id : 0;
    if (!connecting() && (curId != m_loadedItem || itemsChanged)) {
        if (curId != m_loadedItem)
            m_startTimer.stop();
        m_player->syncQueue(tracks, cur ? st.index : -1);
        if (curId != m_loadedItem)
            m_endedTrack = 0;
        m_loadedItem = curId;
        m_loadAt = Jam::nowMs();
    }
    emit changed();
    // Différé : l'état peut changer pendant tick() (l'hôte applique ses propres demandes).
    QMetaObject::invokeMethod(this, &JamController::tick, Qt::QueuedConnection);
}

// ---------- Synchronisation ----------

void JamController::setStatus(const QString &s)
{
    if (s != m_status) {
        m_status = s;
        emit statusChanged();
    }
}

void JamController::tick()
{
    const Jam::State &st = m_session.state();
    const Jam::Item *cur = st.current();
    if (!active())
        return;
    if (connecting()) {
        setStatus(QStringLiteral("Connexion…"));
        return;
    }
    if (!cur) {
        setStatus(st.items.isEmpty() ? QStringLiteral("Ajoutez des morceaux pour lancer le Jam")
                                     : QStringLiteral("File terminée : ajoutez des morceaux pour continuer"));
        return;
    }
    const qint64 now = Jam::nowMs();
    const auto playing = m_player->current();
    if (!playing || playing->ytId != cur->ytId)
        return;
    if (!m_player->isReady()) {
        if (playing->id == m_endedTrack) {
            setStatus(QStringLiteral("Morceau suivant…"));
            return;
        }
        // Échec de chargement (réseau, YouTube…) : nouvel essai régulier.
        if (!m_player->loading() && now - m_loadAt > 15000) {
            m_player->syncQueue(m_player->queue(), m_player->index());
            m_loadAt = now;
        }
        setStatus(m_player->status().isEmpty() ? QStringLiteral("Chargement du morceau…") : m_player->status());
        return;
    }
    if (m_readyItem != cur->id) {
        // Marqué avant l'envoi : chez l'hôte, la demande est appliquée immédiatement.
        m_readyItem = cur->id;
        m_session.reportReady(cur->id);
    }

    const double pos = m_player->position();
    if (st.waiting || !st.playing) {
        // Attente des autres ou pause : immobile à la position commune.
        m_startTimer.stop();
        m_player->setSpeed(1.0);
        m_player->setPaused(true);
        const double target = st.waiting ? 0 : st.anchorPos;
        if (qAbs(pos - target) > 0.15 && now - m_lastSeek > 500) {
            m_player->seek(target);
            m_lastSeek = now;
        }
        if (st.waiting) {
            QStringList late;
            for (const Jam::Member &m : st.members)
                if (!m.ready)
                    late << m.name;
            setStatus(QStringLiteral("En attente de : %1").arg(late.join(", ")));
        } else {
            setStatus(QStringLiteral("En pause"));
        }
        return;
    }

    const double expected = st.positionAt(m_session.hostNow()) + m_latencyMs / 1000.0;
    if (expected < 0) {
        // Départ programmé : prêt à la bonne position, lancement à l'instant convenu.
        m_player->setSpeed(1.0);
        m_player->setPaused(true);
        if (qAbs(pos - st.anchorPos) > 0.1 && now - m_lastSeek > 300) {
            m_player->seek(st.anchorPos);
            m_lastSeek = now;
        }
        const int delay = int(-expected * 1000);
        if (!m_startTimer.isActive() || qAbs(m_startTimer.remainingTime() - delay) > 30)
            m_startTimer.start(delay);
        setStatus(QStringLiteral("Départ synchronisé…"));
        return;
    }
    m_startTimer.stop();
    if (cur->duration > 0 && expected > cur->duration) {
        setStatus(QStringLiteral("Morceau suivant…"));
        return;
    }
    const double diff = pos - expected;
    const double threshold = m_player->source() == "stream" ? kSeekThresholdStream : kSeekThresholdLocal;
    if (qAbs(diff) > threshold) {
        if (now - m_lastSeek > 1500) {
            m_player->seek(expected);
            m_lastSeek = now;
        }
        m_player->setSpeed(1.0);
    } else if (qAbs(diff) > kSpeedDeadZone) {
        // En avance : on ralentit un peu ; en retard : on accélère (inaudible à ±5 %).
        m_player->setSpeed(1.0 - qBound(-0.05, diff * 0.5, 0.05));
    } else {
        m_player->setSpeed(1.0);
    }
    m_player->setPaused(false);
    setStatus(QStringLiteral("Synchronisé · écart %1 ms").arg(qRound(qAbs(diff) * 100) * 10));
}

// ---------- Commandes ----------

int JamController::add(const QList<Track> &tracks)
{
    QList<Jam::Item> items;
    for (const Track &t : tracks) {
        if (!Jam::isValidYtId(t.ytId))
            continue;
        Jam::Item it;
        it.ytId = t.ytId;
        it.title = t.title;
        it.artist = t.artist;
        it.duration = t.duration;
        it.thumbnail = t.thumbnail.startsWith("https://") ? t.thumbnail : QString();
        items.append(it);
    }
    if (!items.isEmpty())
        m_session.add(items);
    return int(items.size());
}

void JamController::togglePlay()
{
    if (m_session.state().playing)
        m_session.pause();
    else
        m_session.play();
}

void JamController::jumpTo(int row)
{
    const auto &items = m_session.state().items;
    if (row >= 0 && row < items.size())
        m_session.jump(items[row].id);
}

void JamController::removeAt(int row)
{
    const auto &items = m_session.state().items;
    if (row >= 0 && row < items.size())
        m_session.remove(items[row].id);
}
