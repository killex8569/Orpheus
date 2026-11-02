#pragma once

#include <QString>

// Emplacements des données (convention XDG, surchargeable par $ORPHEUS_HOME).
struct Paths {
    QString data;
    QString library;
    QString covers;
    QString bin;  // copie de yt-dlp gérée par Orpheus
    QString db;

    static Paths resolve();
    QString coverFile(const QString &ytId) const;
};

// Cherche un exécutable dans le PATH puis dans quelques emplacements courants.
QString findExecutable(const QString &name);
