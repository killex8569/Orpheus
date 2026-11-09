#pragma once

// Android uniquement : pont vers android/src/io/github/killex8569/orpheus/OrpheusAndroid.java.

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <functional>

class QObject;

namespace AndroidBridge {

// Python, yt-dlp, ffmpeg et QuickJS embarqués par la bibliothèque youtubedl-android.
// Android n'exécute que les binaires du dossier des bibliothèques natives de l'APK :
// yt-dlp tourne donc avec « libpython.so », dans l'environnement que prépare la bibliothèque.
struct YtDlpLaunch {
    QString python;          // <nativeLibraryDir>/libpython.so
    QString script;          // yt-dlp (copié par la bibliothèque)
    QString quickjs;         // moteur JavaScript demandé par YouTube
    QStringList extraArgs;   // --ffmpeg-location, --cache-dir
    QProcessEnvironment env;
};
YtDlpLaunch ytdlpLaunch();

// Décompresse Python et ffmpeg au premier lancement (quelques secondes), hors du thread
// principal ; `done` est appelé dans le thread principal si `receiver` existe encore.
void initYtDlp(QObject *receiver, std::function<void(QString error)> done);
void updateYtDlp(QObject *receiver, std::function<void(QString message, bool ok)> done);

// Réception des diffusions UDP du Jam (filtrées par le Wi-Fi sans ce verrou).
void setMulticastLock(bool on);
// Notification de lecture et commandes (écran verrouillé, casque) ; titre vide = arrêt.
void updateMedia(const QString &title, const QString &artist, bool playing);
void requestNotificationPermission();
// Bouton Retour pendant la lecture : l'application passe en arrière-plan au lieu de quitter.
void moveTaskToBack();

}  // namespace AndroidBridge
