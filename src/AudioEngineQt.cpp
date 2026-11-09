// Lecture par Qt Multimedia : Android (libmpv n'y est pas disponible).

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

#include "AudioEngine.h"

namespace {

class QtEngine : public AudioEngine {
public:
    QtEngine(double volume, QObject *parent) : AudioEngine(parent)
    {
        m_player.setAudioOutput(&m_output);
        setVolume(volume);
        connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (!m_active)
                return;
            if ((s == QMediaPlayer::BufferedMedia || s == QMediaPlayer::LoadedMedia) && !m_started) {
                m_started = true;
                emit loaded();
                emit restarted();
            } else if (s == QMediaPlayer::EndOfMedia) {
                m_active = false;
                emit finished(false, {});
            } else if (s == QMediaPlayer::InvalidMedia) {
                m_active = false;
                emit finished(true, m_player.errorString().isEmpty() ? QStringLiteral("fichier illisible") : m_player.errorString());
            }
        });
        connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error e, const QString &msg) {
            if (!m_active || e == QMediaPlayer::NoError)
                return;
            m_active = false;
            emit finished(true, msg.isEmpty() ? QStringLiteral("erreur de lecture") : msg);
        });
        connect(&m_player, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState s) { emit pausedChanged(s != QMediaPlayer::PlayingState); });
        connect(&m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
            if (ms > 0)
                emit durationChanged(ms / 1000.0);
        });
    }

    // shortcut: titre et User-Agent ignorés (QMediaPlayer n'a pas d'en-têtes HTTP) ; la
    // lecture passe surtout par le fichier téléchargé, le flux n'est qu'un repli.
    bool load(const QString &target, const QString &, const QString &, bool paused) override
    {
        const QUrl url = target.contains(QLatin1String("://")) ? QUrl(target) : QUrl::fromLocalFile(target);
        if (!url.isValid())
            return false;
        m_active = false;  // pas de signal pour le morceau remplacé
        m_player.stop();
        m_started = false;
        m_active = true;
        m_player.setSource(url);
        paused ? m_player.pause() : m_player.play();
        return true;
    }

    void stop() override
    {
        m_active = false;
        m_player.stop();
    }

    void setPaused(bool paused) override { paused ? m_player.pause() : m_player.play(); }
    void togglePause() override { setPaused(m_player.playbackState() == QMediaPlayer::PlayingState); }

    void seek(double seconds) override
    {
        m_player.setPosition(qint64(qMax(0.0, seconds) * 1000));
        emit restarted();
    }

    void setVolume(double volume) override { m_output.setVolume(float(qBound(0.0, volume / 100.0, 1.0))); }
    void setSpeed(double speed) override { m_player.setPlaybackRate(speed); }
    double position() const override { return m_player.position() / 1000.0; }

private:
    QAudioOutput m_output;  // déclarée avant : détruite après le lecteur qui l'utilise
    QMediaPlayer m_player;
    bool m_active = false;   // un morceau est chargé et ses signaux comptent
    bool m_started = false;  // « loaded » déjà émis pour ce morceau
};

}  // namespace

AudioEngine *AudioEngine::create(Database *, double volume, QObject *parent)
{
    return new QtEngine(volume, parent);
}
