// Orpheus — lecteur de musique de bureau avec cache local intelligent.

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("orpheus");
    app.setApplicationDisplayName("Orpheus");
    app.setDesktopFileName("orpheus");
    app.setWindowIcon(QIcon(":/qt/qml/Orpheus/icons/music.svg"));
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
    engine.rootContext()->setContextProperty("backend", &backend);
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

    // ORPHEUS_VIEW=lyrics|stats|… : vue affichée au démarrage (captures d'écran).
    if (const QString v = qEnvironmentVariable("ORPHEUS_VIEW"); !v.isEmpty() && !engine.rootObjects().isEmpty())
        engine.rootObjects().first()->setProperty("view", v);

    if (const QString url = qEnvironmentVariable("ORPHEUS_DEMO_IMPORT"); !url.isEmpty())
        backend.importUrl(url);

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
        delete instanceServer.nextPendingConnection();
        for (QObject *o : engine.rootObjects())
            if (auto *w = qobject_cast<QQuickWindow *>(o)) {
                w->show();
                w->raise();
                w->requestActivate();
            }
    });
    return app.exec();
}
