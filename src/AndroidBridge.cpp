#include "AndroidBridge.h"

#include <QCoreApplication>
#include <QJniObject>
#include <QPointer>
#include <QThreadPool>
#include <QtCore/qcoreapplication_platform.h>

namespace {

constexpr char kClass[] = "io/github/killex8569/orpheus/OrpheusAndroid";
constexpr char kCtxToString[] = "(Landroid/content/Context;)Ljava/lang/String;";

jobject context()
{
    // Référence gardée toute la vie de l'application : celle d'un objet temporaire serait
    // libérée avant l'appel Java (Context nul côté Java).
    static const QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    return ctx.object();
}

QString callString(const char *method)
{
    return QJniObject::callStaticObjectMethod(kClass, method, kCtxToString, context()).toString();
}

// Appel Java bloquant (décompression, réseau) dans un thread de travail ; réponse dans
// le thread principal.
void runBlocking(QObject *receiver, const char *method, std::function<void(QString)> done)
{
    QPointer<QObject> guard = receiver;
    QThreadPool::globalInstance()->start([guard, method, done] {
        const QString result = callString(method);
        QMetaObject::invokeMethod(
            qApp, [guard, done, result] {
                if (guard)
                    done(result);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace

namespace AndroidBridge {

YtDlpLaunch ytdlpLaunch()
{
    // Mêmes chemins et variables que YoutubeDL.execute() (youtubedl-android 0.18.1).
    static const YtDlpLaunch launch = [] {
        const QString lib = callString("nativeLibDir");
        const QString base = callString("noBackupDir") + "/youtubedl-android";
        const QString cache = callString("cacheDir");
        const QString python = base + "/packages/python";
        YtDlpLaunch l;
        l.python = lib + "/libpython.so";
        l.script = base + "/yt-dlp/yt-dlp";
        l.quickjs = lib + "/libqjs.so";
        l.extraArgs = {"--ffmpeg-location", lib + "/libffmpeg.so", "--cache-dir", cache + "/yt-dlp"};
        l.env = QProcessEnvironment::systemEnvironment();
        l.env.insert("LD_LIBRARY_PATH", python + "/usr/lib:" + base + "/packages/ffmpeg/usr/lib:" + base
                                            + "/packages/aria2c/usr/lib");
        l.env.insert("SSL_CERT_FILE", python + "/usr/etc/tls/cert.pem");
        l.env.insert("PATH", l.env.value("PATH") + ":" + lib);
        l.env.insert("PYTHONHOME", python + "/usr");
        l.env.insert("HOME", python + "/usr");
        l.env.insert("TMPDIR", cache);
        return l;
    }();
    return launch;
}

void initYtDlp(QObject *receiver, std::function<void(QString)> done)
{
    runBlocking(receiver, "initYtDlp", std::move(done));
}

void updateYtDlp(QObject *receiver, std::function<void(QString, bool)> done)
{
    // Réponse Java : « ok:message » ou « error:message ».
    runBlocking(receiver, "updateYtDlp", [done](QString r) { done(r.section(':', 1), r.startsWith(QLatin1String("ok:"))); });
}

void setMulticastLock(bool on)
{
    QJniObject::callStaticMethod<void>(kClass, "setMulticastLock", "(Landroid/content/Context;Z)V", context(), jboolean(on));
}

void updateMedia(const QString &title, const QString &artist, bool playing)
{
    QJniObject::callStaticMethod<void>(kClass, "updateMedia", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Z)V",
                                       context(), QJniObject::fromString(title).object<jstring>(),
                                       QJniObject::fromString(artist).object<jstring>(), jboolean(playing));
}

void requestNotificationPermission()
{
    QJniObject::callStaticMethod<void>(kClass, "requestNotificationPermission", "(Landroid/content/Context;)V", context());
}

void moveTaskToBack()
{
    QJniObject::callStaticMethod<void>(kClass, "moveTaskToBack", "(Landroid/content/Context;)V", context());
}

}  // namespace AndroidBridge
