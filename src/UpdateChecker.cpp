#include "UpdateChecker.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QRegularExpression>

#ifndef ORPHEUS_VERSION
#define ORPHEUS_VERSION "0.0.0"
#endif
#ifndef ORPHEUS_REPO
#define ORPHEUS_REPO "killex8569/Orpheus"
#endif
#ifndef ORPHEUS_SOURCE_DIR
#define ORPHEUS_SOURCE_DIR ""
#endif

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

QString UpdateChecker::currentVersion()
{
    return QStringLiteral(ORPHEUS_VERSION);
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    // « v1.2.3-beta » → [1, 2, 3] ; les parties absentes valent 0.
    auto parts = [](const QString &v) {
        static const QRegularExpression re(QStringLiteral("^[vV]?(\\d+(?:\\.\\d+)*)"));
        QList<int> out;
        const auto m = re.match(v.trimmed());
        if (m.hasMatch())
            for (const QString &p : m.captured(1).split('.'))
                out << p.toInt();
        return out;
    };
    const QList<int> pa = parts(a), pb = parts(b);
    for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i, 0), y = pb.value(i, 0);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QString UpdateChecker::publishedText() const
{
    return m_published.isValid() ? QLocale().toString(m_published.toLocalTime().date(), QLocale::LongFormat) : QString();
}

QString UpdateChecker::lastCheckText() const
{
    return m_lastCheck.isValid() ? m_lastCheck.toLocalTime().toString(QStringLiteral("HH:mm")) : QString();
}

QString UpdateChecker::updateCommand() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(ORPHEUS_PACKAGE)
    // Installé par un installeur ou un paquet : la mise à jour passe par celui de la release.
    return {};
#endif
    const QString src = QStringLiteral(ORPHEUS_SOURCE_DIR);
    if (!src.isEmpty() && QDir(src + "/.git").exists())
        return QStringLiteral("cd \"%1\" && git pull && ./install.sh").arg(src);
    return QStringLiteral("git clone https://github.com/%1.git && cd %2 && ./install.sh")
        .arg(QStringLiteral(ORPHEUS_REPO), QStringLiteral(ORPHEUS_REPO).section('/', 1));
}

void UpdateChecker::check()
{
    if (m_status == QLatin1String("checking"))
        return;
    m_status = QStringLiteral("checking");
    m_error.clear();
    emit changed();

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(ORPHEUS_REPO)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("Orpheus/") + ORPHEUS_VERSION);
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_lastCheck = QDateTime::currentDateTimeUtc();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (http == 404) {
            m_status = QStringLiteral("error");
            m_error = QStringLiteral("Aucune release publiée sur GitHub pour le moment.");
        } else if (http == 403 || http == 429) {
            m_status = QStringLiteral("error");
            m_error = QStringLiteral("GitHub limite le nombre de vérifications : réessayez dans une heure.");
        } else if (reply->error() != QNetworkReply::NoError || o.value("tag_name").toString().isEmpty()) {
            m_status = QStringLiteral("error");
            m_error = QStringLiteral("Impossible de joindre GitHub (connexion Internet ?).");
        } else {
            m_latest = o.value("tag_name").toString().left(40);
            m_url = o.value("html_url").toString();
            if (!m_url.startsWith("https://github.com/"))
                m_url = QStringLiteral("https://github.com/%1/releases").arg(ORPHEUS_REPO);
            m_notes = o.value("body").toString().left(20000);
            m_published = QDateTime::fromString(o.value("published_at").toString(), Qt::ISODate);
            const bool newer = compareVersions(m_latest, currentVersion()) > 0;
            m_status = newer ? QStringLiteral("available") : QStringLiteral("uptodate");
            if (newer)
                emit updateFound(m_latest);
        }
        emit changed();
    });
}
