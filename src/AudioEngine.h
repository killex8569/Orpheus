#pragma once

#include <QObject>

class Database;

// Moteur de lecture audio : libmpv sur ordinateur (AudioEngineMpv.cpp), Qt Multimedia
// sous Android (AudioEngineQt.cpp). Un seul des deux est compilé ; Player s'appuie
// dessus pour la file, la stratégie de chargement et les statistiques.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    static AudioEngine *create(Database *db, double volume, QObject *parent);

    // Charge un fichier local ou une URL de flux ; false si le moteur la refuse.
    virtual bool load(const QString &target, const QString &title, const QString &userAgent, bool paused) = 0;
    virtual void stop() = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void togglePause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void setVolume(double volume) = 0;  // 0 à 130
    virtual void setSpeed(double speed) = 0;
    virtual double position() const = 0;

signals:
    void loaded();                                       // lecture du morceau chargé démarrée
    void finished(bool error, const QString &message);   // fin du morceau chargé (ou erreur)
    void restarted();                                    // lecture repartie (saut, début)
    void pausedChanged(bool paused);
    void durationChanged(double seconds);
    void volumeChanged(double volume);
};
