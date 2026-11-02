#include "Mpris.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

#include "Backend.h"

namespace {

const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

QString trackPath(qint64 id)
{
    // Chemin D-Bus valide : chiffres et lettres uniquement.
    return id > 0 ? QStringLiteral("/org/orpheus/track/%1").arg(id) : QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
}

qlonglong toUs(double seconds)
{
    return qlonglong(seconds * 1e6);
}

}  // namespace

// ---------- Mpris ----------

Mpris::Mpris(Backend *backend, std::function<void()> raise, QObject *parent)
    : QObject(parent), m_backend(backend), m_raise(std::move(raise))
{
    new MprisRootAdaptor(this);
    auto *player = new MprisPlayerAdaptor(this);
    connect(backend, &Backend::trackChanged, this, &Mpris::notifyChanges);
    connect(backend, &Backend::playerChanged, this, &Mpris::notifyChanges);
    connect(backend, &Backend::seeked, player, [this, player] { emit player->Seeked(toUs(m_backend->position())); });
}

bool Mpris::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerObject(kObjectPath, this)) {
        qWarning() << "MPRIS : enregistrement de l'objet impossible";
        return false;
    }
    if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.orpheus"))) {
        qWarning() << "MPRIS : service indisponible :" << bus.lastError().message();
        bus.unregisterObject(kObjectPath);
        return false;
    }
    notifyChanges();
    return true;
}

QString Mpris::playbackStatus() const
{
    if (!m_backend->hasTrack())
        return QStringLiteral("Stopped");
    // Pendant un chargement, le morceau est considéré comme en lecture.
    return m_backend->paused() && !m_backend->loading() ? QStringLiteral("Paused") : QStringLiteral("Playing");
}

QVariantMap Mpris::metadata() const
{
    QVariantMap m;
    if (!m_backend->hasTrack())
        return m;
    m.insert("mpris:trackid", QVariant::fromValue(QDBusObjectPath(trackPath(m_backend->currentTrackId()))));
    m.insert("xesam:title", m_backend->title());
    if (!m_backend->artist().isEmpty())
        m.insert("xesam:artist", QStringList{m_backend->artist()});
    if (!m_backend->album().isEmpty())
        m.insert("xesam:album", m_backend->album());
    if (m_backend->duration() > 0)
        m.insert("mpris:length", toUs(m_backend->duration()));
    if (!m_backend->cover().isEmpty())
        m.insert("mpris:artUrl", m_backend->cover());
    return m;
}

void Mpris::notifyChanges()
{
    const QVariantMap current{
        {"PlaybackStatus", playbackStatus()},
        {"LoopStatus", QStringList{"None", "Playlist", "Track"}.value(m_backend->repeat())},
        {"Shuffle", m_backend->shuffle()},
        {"Volume", m_backend->volume() / 100.0},
        {"CanGoNext", m_backend->canGoNext()},
        {"CanGoPrevious", m_backend->canGoPrevious()},
        {"CanPlay", m_backend->hasTrack()},
        {"CanPause", m_backend->hasTrack()},
        {"CanSeek", m_backend->hasTrack()},
    };
    QVariantMap changed;
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        if (m_sent.value(it.key()) != it.value())
            changed.insert(it.key(), it.value());
    m_sent = current;
    // Métadonnées comparées par leur contenu textuel (chemin D-Bus inclus).
    const QVariantMap meta = metadata();
    QStringList fingerprint;
    for (auto it = meta.cbegin(); it != meta.cend(); ++it)
        fingerprint << it.key() + '=' + (it.key() == "mpris:trackid" ? it->value<QDBusObjectPath>().path() : it->toString())
                           + it->toStringList().join(',');
    if (const QString f = fingerprint.join('\n'); f != m_sentMetadata) {
        m_sentMetadata = f;
        changed.insert("Metadata", meta);
    }
    if (changed.isEmpty())
        return;
    QDBusMessage signal = QDBusMessage::createSignal(kObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                     QStringLiteral("PropertiesChanged"));
    signal << kPlayerInterface << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

// ---------- org.mpris.MediaPlayer2 ----------

void MprisRootAdaptor::Quit()
{
    QCoreApplication::quit();
}

// ---------- org.mpris.MediaPlayer2.Player ----------

// PropertiesChanged est émis à la main par Mpris::notifyChanges.
MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *parent) : QDBusAbstractAdaptor(parent), m_mpris(parent) {}

QString MprisPlayerAdaptor::loopStatus() const
{
    return QStringList{"None", "Playlist", "Track"}.value(m_mpris->backend()->repeat(), "None");
}

void MprisPlayerAdaptor::setLoopStatus(const QString &s)
{
    const int mode = QStringList{"None", "Playlist", "Track"}.indexOf(s);
    if (mode >= 0)
        m_mpris->backend()->setRepeat(mode);
}

bool MprisPlayerAdaptor::shuffle() const
{
    return m_mpris->backend()->shuffle();
}

void MprisPlayerAdaptor::setShuffle(bool on)
{
    m_mpris->backend()->setShuffle(on);
}

double MprisPlayerAdaptor::volume() const
{
    return m_mpris->backend()->volume() / 100.0;
}

void MprisPlayerAdaptor::setVolume(double v)
{
    m_mpris->backend()->setVolume(qBound(0.0, v, 1.3) * 100.0);
}

qlonglong MprisPlayerAdaptor::position() const
{
    return toUs(m_mpris->backend()->position());
}

bool MprisPlayerAdaptor::canGoNext() const
{
    return m_mpris->backend()->canGoNext();
}

bool MprisPlayerAdaptor::canGoPrevious() const
{
    return m_mpris->backend()->canGoPrevious();
}

bool MprisPlayerAdaptor::hasTrack() const
{
    return m_mpris->backend()->hasTrack();
}

void MprisPlayerAdaptor::Next()
{
    m_mpris->backend()->next();
}

void MprisPlayerAdaptor::Previous()
{
    m_mpris->backend()->prev();
}

void MprisPlayerAdaptor::Pause()
{
    if (m_mpris->playbackStatus() == QLatin1String("Playing"))
        m_mpris->backend()->togglePlay();
}

void MprisPlayerAdaptor::PlayPause()
{
    m_mpris->backend()->togglePlay();
}

void MprisPlayerAdaptor::Stop()
{
    Pause();
}

void MprisPlayerAdaptor::Play()
{
    if (m_mpris->playbackStatus() != QLatin1String("Playing"))
        m_mpris->backend()->togglePlay();
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    Backend *b = m_mpris->backend();
    const double target = b->position() + offsetUs / 1e6;
    // Au-delà de la fin : morceau suivant, comme le prévoit la spécification.
    if (b->duration() > 0 && target >= b->duration())
        b->next();
    else
        b->seek(qMax(0.0, target));
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    Backend *b = m_mpris->backend();
    // Ignoré si le morceau a changé entre-temps (spécification MPRIS).
    if (trackId.path() != trackPath(b->currentTrackId()) || positionUs < 0)
        return;
    if (b->duration() <= 0 || positionUs / 1e6 <= b->duration())
        b->seek(positionUs / 1e6);
}
