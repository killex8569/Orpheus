#include "Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Paths Paths::resolve()
{
    Paths p;
    const QByteArray custom = qgetenv("ORPHEUS_HOME");
    if (!custom.isEmpty()) {
        p.data = QString::fromLocal8Bit(custom);
    } else {
        p.data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/orpheus";
    }
    p.library = p.data + "/library";
    p.covers = p.data + "/covers";
    p.bin = p.data + "/bin";
    p.db = p.data + "/orpheus.db";
    for (const QString &dir : {p.data, p.library, p.covers, p.bin})
        QDir().mkpath(dir);
    return p;
}

QString Paths::coverFile(const QString &ytId) const
{
    return covers + "/" + ytId + ".jpg";
}

QString findExecutable(const QString &name)
{
    QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
        return found;
    const QString home = QDir::homePath();
    return QStandardPaths::findExecutable(
        name, {home + "/.local/bin", home + "/.deno/bin", "/usr/local/bin", "/usr/bin"});
}
