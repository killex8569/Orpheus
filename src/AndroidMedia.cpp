// Android : notification de lecture et commandes du système (écran verrouillé, casque,
// Bluetooth), l'équivalent de MPRIS sous Linux. Voir OrpheusService.java.

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QPointer>

#include "AndroidBridge.h"
#include "Backend.h"

namespace {

QPointer<Backend> g_backend;

// Appelé par Java depuis le thread principal d'Android : relayé au thread de Qt.
void JNICALL nativeCommand(JNIEnv *, jclass, jstring command)
{
    const QString c = QJniObject(command).toString();
    QMetaObject::invokeMethod(
        qApp, [c] {
            Backend *b = g_backend;
            if (!b)
                return;
            if (c == QLatin1String("toggle") || (c == QLatin1String("play") && b->paused())
                || (c == QLatin1String("pause") && !b->paused()))
                b->togglePlay();
            else if (c == QLatin1String("next"))
                b->next();
            else if (c == QLatin1String("prev"))
                b->prev();
        },
        Qt::QueuedConnection);
}

}  // namespace

void setupAndroidMedia(Backend *backend)
{
    g_backend = backend;
    const JNINativeMethod methods[] = {{"nativeCommand", "(Ljava/lang/String;)V", reinterpret_cast<void *>(nativeCommand)}};
    QJniEnvironment().registerNativeMethods("io/github/killex8569/orpheus/OrpheusAndroid", methods, 1);
    AndroidBridge::requestNotificationPermission();

    // Notification mise à jour seulement quand ce qu'elle affiche change.
    auto update = [backend] {
        static QString lastTitle, lastArtist;
        static bool lastPlaying = false;
        const QString title = backend->hasTrack() ? backend->title() : QString();
        const QString artist = backend->artist();
        const bool playing = !backend->paused() || backend->loading();
        if (title == lastTitle && artist == lastArtist && playing == lastPlaying)
            return;
        lastTitle = title;
        lastArtist = artist;
        lastPlaying = playing;
        AndroidBridge::updateMedia(title, artist, playing);
    };
    QObject::connect(backend, &Backend::trackChanged, backend, update);
    QObject::connect(backend, &Backend::playerChanged, backend, update);
}
