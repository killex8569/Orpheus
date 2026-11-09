// Lecture par libmpv (intégrée au processus) : Linux, Windows, macOS.

#include <QCoreApplication>
#include <mpv/client.h>

#include "AudioEngine.h"
#include "Database.h"

namespace {

// Encode une valeur d'option mpv sans risque avec les virgules (`%longueur%valeur`).
QByteArray mpvQuote(const QString &s)
{
    const QByteArray u = s.toUtf8();
    return "%" + QByteArray::number(u.size()) + "%" + u;
}

class MpvEngine : public AudioEngine {
public:
    MpvEngine(Database *db, double volume, QObject *parent) : AudioEngine(parent)
    {
        m_mpv = mpv_create();
        if (!m_mpv)
            qFatal("libmpv : création impossible");
        auto opt = [this](const char *k, const QByteArray &v) { mpv_set_option_string(m_mpv, k, v.constData()); };
        opt("vid", "no");
        opt("video", "no");
        opt("vo", "null");
        opt("idle", "yes");
        opt("ytdl", "no");
        opt("terminal", "no");
        opt("input-default-bindings", "no");
        opt("audio-display", "no");
        opt("gapless-audio", "weak");
        opt("cache", "yes");
        opt("demuxer-max-bytes", "64MiB");
        opt("audio-client-name", "Orpheus");
        opt("volume", QByteArray::number(volume));
        if (db->settingBool("audio_exclusive", false))
            opt("audio-exclusive", "yes");
        if (const QString dev = db->setting("audio_device"); !dev.isEmpty())
            opt("audio-device", dev.toUtf8());
        if (mpv_initialize(m_mpv) < 0)
            qFatal("libmpv : initialisation impossible");
        mpv_observe_property(m_mpv, 1, "pause", MPV_FORMAT_FLAG);
        mpv_observe_property(m_mpv, 2, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_mpv, 3, "volume", MPV_FORMAT_DOUBLE);
        // mpv réveille l'application depuis son propre thread : on traite les événements
        // dans la boucle Qt principale.
        mpv_set_wakeup_callback(
            m_mpv,
            [](void *ctx) { QMetaObject::invokeMethod(static_cast<MpvEngine *>(ctx), &MpvEngine::onEvents, Qt::QueuedConnection); },
            this);
    }

    ~MpvEngine() override
    {
        // Plus de réveil depuis le thread de mpv pendant la destruction.
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
    }

    bool load(const QString &target, const QString &title, const QString &userAgent, bool paused) override
    {
        QByteArray opts = "force-media-title=" + mpvQuote(title);
        if (!userAgent.isEmpty())
            opts += ",user-agent=" + mpvQuote(userAgent);
        const QByteArray file = target.toUtf8();
        const char *args[] = {"loadfile", file.constData(), "replace", "-1", opts.constData(), nullptr};
        mpv_node result;
        if (mpv_command_ret(m_mpv, args, &result) < 0)
            return false;
        if (result.format == MPV_FORMAT_NODE_MAP) {
            for (int i = 0; i < result.u.list->num; ++i)
                if (qstrcmp(result.u.list->keys[i], "playlist_entry_id") == 0)
                    m_entryId = result.u.list->values[i].u.int64;
        }
        mpv_free_node_contents(&result);
        int pause = paused ? 1 : 0;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pause);
        return true;
    }

    void stop() override
    {
        const char *args[] = {"stop", nullptr};
        mpv_command_async(m_mpv, 0, args);
    }

    void setPaused(bool paused) override
    {
        int flag = paused ? 1 : 0;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
    }

    void togglePause() override
    {
        const char *args[] = {"cycle", "pause", nullptr};
        mpv_command_async(m_mpv, 0, args);
    }

    void seek(double seconds) override
    {
        const QByteArray p = QByteArray::number(qMax(0.0, seconds));
        const char *args[] = {"seek", p.constData(), "absolute+exact", nullptr};
        mpv_command_async(m_mpv, 0, args);
    }

    void setVolume(double volume) override { mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume); }
    void setSpeed(double speed) override { mpv_set_property_async(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE, &speed); }

    double position() const override
    {
        double pos = 0;
        mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        return pos;
    }

private:
    void onEvents()
    {
        while (m_mpv) {
            mpv_event *ev = mpv_wait_event(m_mpv, 0);
            if (ev->event_id == MPV_EVENT_NONE)
                break;
            switch (ev->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto *p = static_cast<mpv_event_property *>(ev->data);
                if (!p->data)
                    break;
                const QByteArray name(p->name);
                if (name == "pause" && p->format == MPV_FORMAT_FLAG)
                    emit pausedChanged(*static_cast<int *>(p->data));
                else if (name == "duration" && p->format == MPV_FORMAT_DOUBLE)
                    emit durationChanged(*static_cast<double *>(p->data));
                else if (name == "volume" && p->format == MPV_FORMAT_DOUBLE)
                    emit volumeChanged(*static_cast<double *>(p->data));
                break;
            }
            case MPV_EVENT_FILE_LOADED:
                emit loaded();
                break;
            case MPV_EVENT_PLAYBACK_RESTART:
                emit restarted();
                break;
            case MPV_EVENT_END_FILE: {
                auto *e = static_cast<mpv_event_end_file *>(ev->data);
                if (e->playlist_entry_id != m_entryId)
                    break;  // fin d'un morceau déjà remplacé
                if (e->reason == MPV_END_FILE_REASON_EOF)
                    emit finished(false, {});
                else if (e->reason == MPV_END_FILE_REASON_ERROR)
                    emit finished(true, QString::fromUtf8(mpv_error_string(e->error)));
                break;
            }
            default:
                break;
            }
        }
    }

    mpv_handle *m_mpv = nullptr;
    qint64 m_entryId = -1;
};

}  // namespace

AudioEngine *AudioEngine::create(Database *db, double volume, QObject *parent)
{
    return new MpvEngine(db, volume, parent);
}
