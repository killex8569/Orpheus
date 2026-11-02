#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>

// Vérifie s'il existe une version plus récente d'Orpheus parmi les releases GitHub
// (dernière release publiée, hors brouillons et pré-versions).
class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    // "" (jamais vérifié) | checking | uptodate | available | error
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QString publishedText READ publishedText NOTIFY changed)
    Q_PROPERTY(QString lastCheckText READ lastCheckText NOTIFY changed)
    Q_PROPERTY(QString updateCommand READ updateCommand CONSTANT)

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    static QString currentVersion();
    // < 0 si a est plus ancienne que b, 0 si égales, > 0 si plus récente
    // (« v1.2.0 », « 1.10 », « 1.0.0-beta » acceptés ; le suffixe est ignoré).
    static int compareVersions(const QString &a, const QString &b);

    QString latestVersion() const { return m_latest; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    bool updateAvailable() const { return m_status == QLatin1String("available"); }
    QString releaseUrl() const { return m_url; }
    QString releaseNotes() const { return m_notes; }
    QString publishedText() const;
    QString lastCheckText() const;
    // Commande à lancer pour mettre à jour (dossier des sources s'il existe encore).
    QString updateCommand() const;

    Q_INVOKABLE void check();

signals:
    void changed();
    void updateFound(const QString &version);

private:
    QNetworkAccessManager m_net;
    QString m_latest, m_status, m_error, m_url, m_notes;
    QDateTime m_published, m_lastCheck;
};
