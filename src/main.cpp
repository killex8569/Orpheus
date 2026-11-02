// Orpheus — lecteur de musique de bureau avec cache local intelligent.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <clocale>

#include "Backend.h"
#ifdef ORPHEUS_MPRIS
#include "Mpris.h"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Installeurs Windows/macOS : ffmpeg et deno sont livrés à côté de l'exécutable.
    // Une application lancée depuis le Finder ne voit pas non plus le PATH du terminal
    // (Homebrew). yt-dlp, lancé par Orpheus, hérite de ce PATH.
    {
        QStringList dirs{QDir::toNativeSeparators(QCoreApplication::applicationDirPath())};
#ifdef Q_OS_MACOS
        dirs << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin");
#endif
        dirs << qEnvironmentVariable("PATH");
        qputenv("PATH", dirs.join(QDir::listSeparator()).toLocal8Bit());
    }
#endif
    app.setApplicationName("orpheus");
    app.setApplicationDisplayName("Orpheus");
    app.setDesktopFileName("orpheus");
    app.setApplicationVersion(UpdateChecker::currentVersion());
    app.setWindowIcon(QIcon(":/qt/qml/Orpheus/icons/orpheus.svg"));
    // libmpv exige la locale « C » pour l'analyse des nombres.
    std::setlocale(LC_NUMERIC, "C");

    // Une seule instance : la seconde réveille la première puis se ferme.
    const QString serverName = QStringLiteral("orpheus-%1").arg(qgetenv("USER"));
    {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }
    QLocalServer::removeServer(serverName);
    QLocalServer instanceServer;
    instanceServer.listen(serverName);

    Backend backend(Paths::resolve());
    QString error;
    if (!backend.init(&error)) {
        qCritical("Base de données inaccessible : %s", qPrintable(error));
        return 1;
    }

    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    auto raiseWindow = [&engine] {
        for (QObject *o : engine.rootObjects())
            if (auto *w = qobject_cast<QQuickWindow *>(o)) {
                w->show();
                w->raise();
                w->requestActivate();
            }
    };
    // MPRIS : contrôle depuis le bureau (widget média, touches multimédia globales…).
#ifdef ORPHEUS_MPRIS
    Mpris mpris(&backend, raiseWindow);
    const bool mprisActive = mpris.registerService();
#else
    // Windows/macOS : pas de MPRIS, les touches multimédia sont gérées par la fenêtre.
    const bool mprisActive = false;
#endif
    engine.rootContext()->setContextProperty("backend", &backend);
    // Avec MPRIS, les touches multimédia passent par le bureau : pas de raccourcis
    // en double dans la fenêtre.
    engine.rootContext()->setContextProperty("mprisActive", mprisActive);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Orpheus", "Main");

    // Outil de développement : ORPHEUS_SCREENSHOT=fichier.png capture la fenêtre après
    // ORPHEUS_SCREENSHOT_DELAY ms (défaut 3000) puis quitte ; ORPHEUS_DEMO_SEARCH lance
    // une recherche et lit le premier résultat dès son arrivée.
    if (const QString shot = qEnvironmentVariable("ORPHEUS_SCREENSHOT"); !shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("ORPHEUS_SCREENSHOT_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 3000, &app, [&engine, shot] {
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0)))
                w->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    }
    if (const QString q = qEnvironmentVariable("ORPHEUS_DEMO_SEARCH"); !q.isEmpty()) {
        auto *conn = new QMetaObject::Connection;
        *conn = QObject::connect(&backend, &Backend::searchChanged, &app, [&backend, conn] {
            if (!backend.searching() && backend.searchResults()->count() > 0) {
                QObject::disconnect(*conn);
                delete conn;
                qInfo() << "démo : lecture de" << backend.searchResults()->at(0)->title;
                backend.play("search", 0);
            }
        });
        backend.search(q, "youtube");
    }

    // ORPHEUS_VIEW=lyrics|stats|playlist:<id>|… : vue affichée au démarrage (captures d'écran).
    if (const QString v = qEnvironmentVariable("ORPHEUS_VIEW"); !v.isEmpty() && !engine.rootObjects().isEmpty())
        QMetaObject::invokeMethod(engine.rootObjects().first(), "navigate", Q_ARG(QVariant, v.section(':', 0, 0)),
                                  Q_ARG(QVariant, v.section(':', 1).toLongLong()));

    if (const QString url = qEnvironmentVariable("ORPHEUS_DEMO_IMPORT"); !url.isEmpty())
        backend.importUrl(url);

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
        delete instanceServer.nextPendingConnection();
        raiseWindow();
    });
    return app.exec();
}
