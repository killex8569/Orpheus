#include "Track.h"

#include <QRegularExpression>

QString Track::quality() const
{
    if (codec.isEmpty())
        return {};
    QString q = codec.toUpper();
    if (bitrate > 0)
        q += QStringLiteral(" · %1 kb/s").arg(qRound(bitrate));
    return q;
}

QVariantMap RemotePlaylist::toMap() const
{
    return {{"id", id}, {"title", title}, {"url", url}, {"count", count}};
}

QString formatTime(double seconds)
{
    if (!(seconds > 0))
        return QStringLiteral("0:00");
    const qint64 s = qint64(seconds);
    const qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QChar('0'));
}

QString thumbUrl(const QString &ytId)
{
    return QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(ytId);
}

QString watchUrl(const QString &ytId)
{
    return QStringLiteral("https://www.youtube.com/watch?v=%1").arg(ytId);
}

QString videoIdFrom(const QString &input)
{
    static const QRegularExpression idRe(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    static const QRegularExpression urlRe(
        QStringLiteral("(?:v=|youtu\\.be/|/shorts/|/embed/)([A-Za-z0-9_-]{11})"));
    const QString s = input.trimmed();
    if (idRe.match(s).hasMatch())
        return s;
    const auto m = urlRe.match(s);
    return m.hasMatch() ? m.captured(1) : QString();
}
