#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <QTimer>

#include "Database.h"
#include "Downloader.h"
#include "Importer.h"
#include "Jam.h"
#include "Lyrics.h"
#include "Matcher.h"
#include "Spotify.h"
#include "SpotifyWeb.h"
#include "Track.h"
#include "YtDlp.h"

#include <QJsonDocument>
#include "UpdateChecker.h"

using namespace Matcher;

class CoreTest : public QObject {
    Q_OBJECT

    // JSON écrit avec des apostrophes (plus lisible dans une chaîne C++), converti ici.
    static QJsonObject json(const char *text)
    {
        return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
    }

    static Track input(const QString &yt, const QString &artist, const QString &title, const QString &sp = {})
    {
        Track t;
        t.ytId = yt;
        t.artist = artist;
        t.title = title;
        t.spotifyId = sp;
        return t;
    }

private slots:
    void normalizeStripsAccents()
    {
        QCOMPARE(normalize("Stromae — Alors on Danse!"), QString("stromae alors on danse"));
        QCOMPARE(normalize("Beyoncé & JAY-Z"), QString("beyonce and jay z"));
    }

    void matchKeyStableAcrossSources()
    {
        QCOMPARE(matchKey("Daft Punk", "One More Time"), matchKey("", "Daft Punk - One More Time (Official Video)"));
        QCOMPARE(matchKey("Gims, Vianney", "La même"), matchKey("GIMS", "La Même (feat. Vianney)"));
    }

    void splitUsesTopicChannel()
    {
        QCOMPARE(splitYoutubeTitle("Alors on danse", "Stromae - Topic"), qMakePair(QString("Stromae"), QString("Alors on danse")));
        QCOMPARE(splitYoutubeTitle("Stromae - Papaoutai (Clip Officiel)", "stromaeVEVO"),
                 qMakePair(QString("Stromae"), QString("Papaoutai")));
        QCOMPARE(splitYoutubeTitle("Papaoutai (Afro Soul) - Stromae", "Stromae"),
                 qMakePair(QString("Stromae"), QString("Papaoutai")));
    }

    void prefersOriginalOverLiveAndCover()
    {
        const QList<Candidate> cands{
            {"Daft Punk - One More Time (Live at Alive 2007)", "Daft Punk", 320},
            {"One More Time - Piano Cover", "PianoGuy", 230},
            {"One More Time", "Daft Punk - Topic", 320.4},
            {"Daft Punk - One More Time (Official Video)", "Daft Punk", 322},
        };
        QCOMPARE(best("Daft Punk", "One More Time", 320, cands), 2);
    }

    void keepsRemixWhenRequested()
    {
        const QList<Candidate> cands{{"Artist - Song", "Artist - Topic", 200}, {"Artist - Song (DJ Remix)", "DJ", 260}};
        QCOMPARE(best("Artist", "Song (DJ Remix)", 260, cands), 1);
    }

    void rejectsUnrelated()
    {
        QCOMPARE(best("Daft Punk", "One More Time", 320, {{"Totally different thing", "Someone", 100}}), -1);
    }

    void urls()
    {
        QCOMPARE(videoIdFrom("https://www.youtube.com/watch?v=FGBhQbmPwH8&t=3"), QString("FGBhQbmPwH8"));
        QCOMPARE(videoIdFrom("https://youtu.be/FGBhQbmPwH8"), QString("FGBhQbmPwH8"));
        QCOMPARE(videoIdFrom("daft punk"), QString());
        QCOMPARE(parseSpotifyUrl("https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M?si=abc"),
                 qMakePair(QString("playlist"), QString("37i9dQZF1DXcBWIGoYBM5M")));
        QCOMPARE(parseSpotifyUrl("spotify:album:4aawyAB9vmqN3uQ7FjRGTy").first, QString("album"));
        QVERIFY(parseSpotifyUrl("https://youtube.com/playlist?list=x").first.isEmpty());
    }

    void lrcParsing()
    {
        const auto lines = parseLrc("[ar: Vald]\n[offset:+500]\n[00:12.14]C'est tout le poids\n"
                                    "[00:03.79][01:03.00]Je vis dans les abysses\n[00:07.4]La lumière\n[00:20.00]\n");
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines[0].text, QString("Je vis dans les abysses"));
        QVERIFY(qAbs(lines[0].time - 3.29) < 0.001);  // offset de +500 ms appliqué
        QVERIFY(qAbs(lines[1].time - 6.9) < 0.001);
        QCOMPARE(lines[3].text, QString());  // ligne vide (pause instrumentale) conservée
        QCOMPARE(lines[4].text, QString("Je vis dans les abysses"));
        QCOMPARE(lyricIndexAt(lines, 0), -1);
        QCOMPARE(lyricIndexAt(lines, 7.0), 1);
        QCOMPARE(lyricIndexAt(lines, 500), 4);
        QCOMPARE(Matcher::primaryArtist("VALD & ANGÈLE"), QString("VALD"));
        QCOMPARE(Matcher::primaryArtist("Vald feat. Angèle"), QString("Vald"));
    }

    void listeningStats()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath("s.db"), nullptr));
        const Track a = db.upsertTrack(input("aaaaaaaaaaa", "VALD", "Abysses"));
        const Track b = db.upsertTrack(input("bbbbbbbbbbb", "Vald feat. Angèle", "Désaccordé"));
        const Track c = db.upsertTrack(input("ccccccccccc", "Angèle", "Balance ton quoi"));
        // Commencer un morceau ne compte pas d'écoute.
        db.markStarted(a.id);
        QCOMPARE(db.track(a.id)->playCount, 0);
        QCOMPARE(db.listenStats(0).plays, 0);
        const qint64 row = db.recordPlay(a.id, 80);
        db.updatePlay(row, 140);
        db.recordPlay(a.id, 100);
        db.recordPlay(b.id, 120);
        db.recordPlay(c.id, 90);
        QCOMPARE(db.track(a.id)->playCount, 2);
        const ListenStats st = db.listenStats(0);
        QCOMPARE(st.plays, 4);
        QCOMPARE(st.tracks, 3);
        QCOMPARE(st.seconds, 450.0);
        QCOMPARE(db.topTracks(0, 10).first().track.id, a.id);
        QCOMPARE(db.topTracks(0, 10).first().plays, 2);
        int distinct = 0;
        const auto artists = db.topArtists(0, 10, &distinct);
        QCOMPARE(distinct, 2);  // « VALD » et « Vald feat. Angèle » regroupés
        QCOMPARE(artists.first().plays, 3);
        QCOMPARE(artists.first().tracks, 2);
        QCOMPARE(db.listenStats(nowSecs() + 10).plays, 0);

        // Paroles : à récupérer pour les morceaux en local, puis gardées en base.
        db.setFile(a.id, "/x.opus", "opus", 130, 10, 146);
        QCOMPARE(db.tracksNeedingLyrics(10).size(), 1);
        StoredLyrics l;
        l.status = "found";
        l.synced = "[00:01.00]x";
        db.saveLyrics(a.id, l);
        QCOMPARE(db.lyrics(a.id)->synced, QString("[00:01.00]x"));
        QVERIFY(db.tracksNeedingLyrics(10).isEmpty());
    }

    void parallelPool()
    {
        // Rien à faire : fin immédiate.
        bool all = false;
        runParallel(0, 3, [](int, std::function<void()>) { QFAIL("aucune tâche attendue"); }, [&] { all = true; });
        QVERIFY(all);

        // Tâches terminées immédiatement (morceaux déjà en local) : pas de récursion, tout est traité.
        int doneCount = 0, allCount = 0;
        runParallel(20000, 3, [&](int, std::function<void()> done) { ++doneCount; done(); }, [&] { ++allCount; });
        QCOMPARE(doneCount, 20000);
        QCOMPARE(allCount, 1);

        // Tâches asynchrones : jamais plus de `width` en même temps, toutes terminées une fois.
        int inflight = 0, peak = 0, finished = 0;
        allCount = 0;
        runParallel(
            10, 3,
            [&](int i, std::function<void()> done) {
                peak = qMax(peak, ++inflight);
                QTimer::singleShot(5 + (i % 3) * 5, [&, done] {
                    --inflight;
                    ++finished;
                    done();
                });
            },
            [&] { ++allCount; });
        QVERIFY(QTest::qWaitFor([&] { return allCount == 1; }, 2000));
        QCOMPARE(finished, 10);
        QCOMPARE(peak, 3);
    }

    void jamProtocol()
    {
        // Identifiants venus du réseau : ils servent à nommer des fichiers.
        QVERIFY(Jam::isValidYtId("dQw4w9WgXcQ"));
        QVERIFY(!Jam::isValidYtId("../../etc/x"));
        QVERIFY(!Jam::isValidYtId("--exec=rm -"));
        // Code d'accès : exactement 4 chiffres ; la preuve dépend du code et du défi.
        QVERIFY(Jam::isValidCode("0420"));
        QVERIFY(!Jam::isValidCode("123"));
        QVERIFY(!Jam::isValidCode("12345"));
        QVERIFY(!Jam::isValidCode("12a4"));
        QCOMPARE(Jam::codeProof("1234", "abc"), Jam::codeProof("1234", "abc"));
        QVERIFY(Jam::codeProof("1234", "abc") != Jam::codeProof("1235", "abc"));
        QVERIFY(Jam::codeProof("1234", "abc") != Jam::codeProof("1234", "abd"));
        QVERIFY(!Jam::codeProof("1234", "abc").contains("1234"));
        QVERIFY(!Jam::Item::fromJson({{"yt", "../../../a"}, {"title", "x"}}));
        const auto it = Jam::Item::fromJson({{"yt", "dQw4w9WgXcQ"}, {"title", "T"}, {"thumb", "file:///etc/passwd"}, {"duration", 1e9}});
        QVERIFY(it);
        QVERIFY(it->thumbnail.isEmpty());
        QCOMPARE(it->duration, 86400.0);

        // Position attendue : ancre + temps écoulé ; départ programmé = négative.
        Jam::State st;
        st.items.append(*it);
        st.playing = true;
        st.anchorPos = 10;
        st.anchorTime = 1000;
        QCOMPARE(st.positionAt(3500), 12.5);
        QCOMPARE(st.positionAt(500), 9.5);
        QCOMPARE(st.positionAt(-9000), 0.0);
        st.playing = false;
        QCOMPARE(st.positionAt(99999), 10.0);
        st.waiting = true;
        QCOMPARE(st.positionAt(99999), 0.0);
        QCOMPARE(Jam::State::fromJson(st.toJson()).items.first().ytId, QString("dQw4w9WgXcQ"));

        // Horloge : on retient la mesure à l'aller-retour le plus court.
        Jam::ClockSync clock;
        clock.addSample(1000, 6100, 1200);  // rtt 200 ms : offset 5000 approximatif
        clock.addSample(2000, 7020, 2040);  // rtt 40 ms : offset 5000 précis
        QCOMPARE(clock.rtt(), qint64(40));
        QCOMPARE(clock.offset(), qint64(5000));
    }

    void jamSession()
    {
        JamSession hostS, clientS;
        QVERIFY(!hostS.host("Jam test", "Alice", "12", 0));  // code invalide refusé
        QVERIFY(hostS.host("Jam test", "Alice", "4821", 0));
        QVERIFY(hostS.port() > 0);
        clientS.join(QHostAddress::LocalHost, hostS.port(), "Bob", "4821");
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().members.size() == 2 && !clientS.connecting(); }, 3000));
        QCOMPARE(clientS.state().name, QString("Jam test"));
        QCOMPARE(clientS.myId(), 2);

        // Un invité ajoute des morceaux : le premier démarre, en attente de tout le monde.
        Jam::Item a, b;
        a.ytId = "aaaaaaaaaaa";
        a.title = "A";
        a.duration = 200;
        b.ytId = "bbbbbbbbbbb";
        b.title = "B";
        clientS.add({a, b});
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().items.size() == 2; }, 3000));
        QVERIFY(clientS.state().waiting);
        QCOMPARE(clientS.state().items.first().addedBy, QString("Bob"));
        const qint64 first = clientS.state().current()->id;

        // Doublons refusés : morceau déjà en cours ou à venir, double envoi (double-clic).
        Jam::Item c;
        c.ytId = "ccccccccccc";
        c.title = "C";
        hostS.add({a, c, c});
        clientS.add({b});
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().items.size() == 3; }, 3000));
        QTest::qWait(100);
        QCOMPARE(hostS.state().items.size(), 3);
        QCOMPARE(hostS.state().items.last().ytId, QString("ccccccccccc"));
        hostS.remove(hostS.state().items.last().id);
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().items.size() == 2; }, 3000));

        hostS.reportReady(first);
        QTest::qWait(50);
        QVERIFY(hostS.state().waiting);  // Bob n'est pas encore prêt
        clientS.reportReady(first);
        QVERIFY(QTest::qWaitFor([&] { return !clientS.state().waiting && clientS.state().playing; }, 3000));
        // Départ programmé dans le futur, à la même heure pour tous.
        QVERIFY(clientS.state().anchorTime > Jam::nowMs());

        // Pause par un invité : tout le monde en pause, à la même position.
        QTest::qWait(900);
        clientS.pause();
        QVERIFY(QTest::qWaitFor([&] { return !hostS.state().playing && !clientS.state().playing; }, 3000));
        QVERIFY(hostS.state().anchorPos > 0 && hostS.state().anchorPos < 1);
        QCOMPARE(clientS.state().anchorPos, hostS.state().anchorPos);

        // Saut, puis morceau suivant ; l'horloge est synchronisée (même machine : ~0).
        hostS.seek(42);
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().anchorPos == 42; }, 3000));
        clientS.next();
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().index == 1 && clientS.state().waiting; }, 3000));
        QVERIFY(qAbs(clientS.hostNow() - Jam::nowMs()) < 50);

        // Retrait du morceau en cours : file terminée.
        clientS.remove(clientS.state().current()->id);
        QVERIFY(QTest::qWaitFor([&] { return clientS.state().items.size() == 1 && !clientS.state().current(); }, 3000));

        // L'hôte termine : l'invité est prévenu et sort du Jam.
        QSignalSpy notices(&clientS, &JamSession::notice);
        hostS.leave();
        QVERIFY(QTest::qWaitFor([&] { return clientS.role() == JamSession::None; }, 3000));
        QVERIFY(!notices.isEmpty());
    }

    void jamAccessCode()
    {
        JamSession hostS;
        QVERIFY(hostS.host("Jam privé", "Alice", "4821", 0));
        auto attempt = [&](const QString &code) {
            JamSession guest;
            QSignalSpy notices(&guest, &JamSession::notice);
            guest.join(QHostAddress::LocalHost, hostS.port(), "Intrus", code);
            (void)QTest::qWaitFor([&] { return guest.role() == JamSession::None || !guest.connecting(); }, 3000);
            const bool joined = guest.role() == JamSession::Client && !guest.connecting();
            const QString msg = notices.isEmpty() ? QString() : notices.first().first().toString();
            guest.leave();
            return qMakePair(joined, msg);
        };
        // Mauvais codes : refusés avec le nombre d'essais, sans entrer dans le Jam.
        auto r = attempt("0000");
        QVERIFY(!r.first);
        QVERIFY(r.second.contains("1 sur 3"));
        r = attempt("1111");
        QVERIFY(r.second.contains("2 sur 3"));
        r = attempt("2222");
        QVERIFY(r.second.contains("ne répondra plus"));
        QCOMPARE(hostS.state().members.size(), 1);
        // Adresse bloquée : même le bon code ne passe plus, et l'hôte ne répond rien.
        r = attempt("4821");
        QVERIFY(!r.first);
        QVERIFY(!r.second.contains("Code"));
        QCOMPARE(hostS.state().members.size(), 1);
        // Nouveau Jam : le blocage est levé.
        QVERIFY(hostS.host("Jam privé 2", "Alice", "4821", 0));
        QVERIFY(attempt("4821").first);
    }

    void jamLookupByName()
    {
        QVERIFY(Jam::jamNameMatches("  soirée   CHEZ moi ", "Soirée chez moi"));
        QVERIFY(Jam::jamNameMatches("alex", "Jam de Alex"));
        QVERIFY(!Jam::jamNameMatches("", "Jam de Alex"));
        QVERIFY(!Jam::jamNameMatches("Alexandre", "Jam de Alex"));
        QVERIFY(Jam::isLanAddress(QHostAddress::LocalHost));
        QVERIFY(!Jam::isLanAddress(QHostAddress("8.8.8.8")));

        JamSession hostS, guest;
        QVERIFY(hostS.host("Soirée chez moi", "Alice", "4821", 0));
        auto lookup = [&](const QString &name) {
            QList<Jam::Discovered> result;
            bool done = false;
            const auto c = connect(&guest, &JamSession::lookupFinished, this,
                                   [&](const QString &, const QList<Jam::Discovered> &found) {
                                       result = found;
                                       done = true;
                                   });
            guest.lookup(name);
            (void)QTest::qWaitFor([&] { return done; }, 4000);
            disconnect(c);
            return result;
        };
        // Trouvé par son nom (casse et espaces ignorés) : adresse et port de l'hôte.
        auto found = lookup("soirée CHEZ moi");
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().port, hostS.port());
        QCOMPARE(found.first().name, QString("Soirée chez moi"));
        QVERIFY(lookup("Autre Jam").isEmpty());
        // Rejoindre avec l'adresse trouvée.
        guest.join(found.first().address, found.first().port, "Bob", "4821");
        QVERIFY(QTest::qWaitFor([&] { return hostS.state().members.size() == 2; }, 3000));
        guest.leave();
        QVERIFY(QTest::qWaitFor([&] { return hostS.state().members.size() == 1; }, 3000));

        // Appareil bloqué pour mauvais codes : le Jam ne lui répond même plus. (Sur une
        // même machine, la recherche peut arriver par chaque adresse locale : l'intrus
        // se fait bloquer sur toutes, comme un appareil distant sur la sienne.)
        const QStringList addresses = QStringList{"127.0.0.1"} + JamSession::localAddresses();
        for (const QString &addr : addresses)
            for (const char *code : {"0000", "1111", "2222"}) {
                JamSession intruder;
                intruder.join(QHostAddress(addr), hostS.port(), "Intrus", code);
                (void)QTest::qWaitFor([&] { return intruder.role() == JamSession::None; }, 3000);
            }
        QVERIFY(lookup("Soirée chez moi").isEmpty());
        // Nouveau Jam (blocage levé) : « Alex » trouve « Jam de Alex ».
        QVERIFY(hostS.host("Jam de Alex", "Alex", "4821", 0));
        QCOMPARE(lookup("alex").size(), 1);
        hostS.leave();
    }

    void versionCompare()
    {
        QCOMPARE(UpdateChecker::compareVersions("v1.0.0", "1.0.0"), 0);
        QVERIFY(UpdateChecker::compareVersions("v1.0.1", "1.0.0") > 0);
        QVERIFY(UpdateChecker::compareVersions("v1.10.0", "v1.9.9") > 0);  // numérique, pas alphabétique
        QVERIFY(UpdateChecker::compareVersions("v2", "1.9.9") > 0);
        QCOMPARE(UpdateChecker::compareVersions("1.2", "1.2.0"), 0);
        QVERIFY(UpdateChecker::compareVersions("v0.9.0", "1.0.0") < 0);
        QCOMPARE(UpdateChecker::compareVersions("v1.1.0-beta", "1.1.0"), 0);
        QVERIFY(!UpdateChecker::currentVersion().isEmpty());
    }

    void pastedSpotifyLinks()
    {
        // Copie depuis l'application Spotify (Ctrl+A, Ctrl+C) : un lien par ligne, doublons possibles.
        const QString pasted = "https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC?si=1a2b\n"
                               "https://open.spotify.com/intl-fr/track/0VjIjW4GlUZAMYd2vXMi3b\r\n"
                               "spotify:track:4uLU6hMCjMI75M1A2tKUQC\n"
                               "https://open.spotify.com/playlist/2CFaMroNgUoEvzbXdI7BIt\n";
        QCOMPARE(spotifyTrackIds(pasted), QStringList({"4uLU6hMCjMI75M1A2tKUQC", "0VjIjW4GlUZAMYd2vXMi3b"}));
        QVERIFY(spotifyTrackIds("rien à voir").isEmpty());
    }

    void spotifyWebTotp()
    {
        // Secret v61 du lecteur web ; valeurs de référence calculées par une implémentation
        // indépendante (Python) dont le code a été accepté par open.spotify.com/api/token.
        const QList<int> v61{44, 55, 47, 42, 70, 40, 34, 114, 76, 74, 50, 111, 120, 97, 75, 76, 94, 102, 43, 69, 49, 120, 118, 80, 64, 78};
        QCOMPARE(spotifyTotp(v61, 0), QString("204513"));
        QCOMPARE(spotifyTotp(v61, 59), QString("332823"));
        QCOMPARE(spotifyTotp(v61, 1793697517), QString("993659"));
        QCOMPARE(spotifyTotp(v61, 2000000000), QString("952533"));
        // Même fenêtre de 30 s → même code.
        QCOMPARE(spotifyTotp(v61, 1793697510), spotifyTotp(v61, 1793697519));
        QCOMPARE(httpDateSecs("Tue, 03 Nov 2026 10:38:37 GMT"), qint64(1793702317));
        QCOMPARE(httpDateSecs("pas une date"), qint64(0));
    }

    void browserCookieFile()
    {
        // Fichier écrit par yt-dlp (--cookies) : tous les cookies du navigateur.
        const QString jar = "# Netscape HTTP Cookie File\n"
                            "# This file is generated by yt-dlp.  Do not edit.\n\n"
                            ".google.com\tTRUE\t/\tTRUE\t4102444800\tsp_dc\tpas-celui-ci\n"
                            ".spotify.com\tTRUE\t/\tTRUE\t1700000000\tsp_dc\tancien\r\n"
                            "#HttpOnly_.open.spotify.com\tTRUE\t/\tTRUE\t4102444800\tsp_dc\tAQB_valeur\n"
                            ".spotify.com\tTRUE\t/\tTRUE\t4102444800\tsp_key\tautre\n"
                            "notspotify.com\tTRUE\t/\tTRUE\t4102444800\tsp_dc\tpiege\n";
        QCOMPARE(netscapeCookie(jar, "spotify.com", "sp_dc"), QString("AQB_valeur"));
        QVERIFY(netscapeCookie(jar, "spotify.com", "absent").isEmpty());
        QVERIFY(netscapeCookie({}, "spotify.com", "sp_dc").isEmpty());
        QCOMPARE(cleanFileName("AC/DC: Live? <2026>. "), QString("AC DC Live 2026"));
    }

    void ytdlpInstallWithoutCurl()
    {
        // curl absent : la mise à jour doit quand même répondre (échec), pas rester muette.
        QTemporaryDir dir;
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", "");
        YtDlp yt(dir.path());
        bool called = false, ok = true;
        yt.updateManaged([&](QString, bool success) {
            called = true;
            ok = success;
        });
        qputenv("PATH", path);
        QTRY_VERIFY_WITH_TIMEOUT(called, 20000);
        QVERIFY(!ok);
    }

    void spotifyWebParsing()
    {
        // Empreintes des requêtes dans le code du lecteur web.
        const QString js = "let fT=new aQ.l(\"fetchPlaylist\",\"query\",\"" + QString(64, 'a')
                           + "\",null),pd=new aQ.l('libraryV3', 'query', '" + QString(64, 'b') + "',null);"
                           + "x(\"addToPlaylist\",\"mutation\",\"" + QString(64, 'c') + "\")";
        const auto hashes = pathfinderHashes(js);
        QCOMPARE(hashes.value("fetchPlaylist"), QString(64, 'a'));
        QCOMPARE(hashes.value("libraryV3"), QString(64, 'b'));
        QCOMPARE(hashes.value("addToPlaylist"), QString(64, 'c'));

        // Réponse de fetchPlaylist (forme relevée sur l'API, réduite).
        const auto playlist = json("{"
                       "'__typename': 'Playlist', 'name': 'Ma playlist',"
                       "'content': {'totalCount': 3, 'items': ["
                       "{'itemV2': {'__typename': 'TrackResponseWrapper', 'data': {'__typename': 'Track', 'name': 'Loser',"
                       "'uri': 'spotify:track:7bxaFZ1O3cHkgLKMsdC3xR', 'trackDuration': {'totalMilliseconds': 223069},"
                       "'artists': {'items': [{'profile': {'name': 'Tame Impala'}}, {'profile': {'name': 'Invité'}}]},"
                       "'albumOfTrack': {'name': 'Deadbeat', 'coverArt': {'sources': ["
                       "{'url': 'https://i.scdn.co/64', 'width': 64}, {'url': 'https://i.scdn.co/300', 'width': 300},"
                       "{'url': 'https://i.scdn.co/640', 'width': 640}]}}}}},"
                       "{'itemV2': {'__typename': 'LocalTrackResponseWrapper', 'data': {'__typename': 'LocalTrack', 'name': 'fichier.mp3'}}},"
                       "{'itemV2': {'__typename': 'EpisodeResponseWrapper', 'data': {'__typename': 'Episode', 'name': 'Podcast'}}}"
                       "]}}");
        int total = 0;
        QString name;
        const QList<Track> tracks = pathfinderPlaylistTracks(playlist, &total, &name);
        QCOMPARE(name, QString("Ma playlist"));
        QCOMPARE(total, 3);
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks[0].title, QString("Loser"));
        QCOMPARE(tracks[0].artist, QString("Tame Impala, Invité"));
        QCOMPARE(tracks[0].album, QString("Deadbeat"));
        QCOMPARE(tracks[0].spotifyId, QString("7bxaFZ1O3cHkgLKMsdC3xR"));
        QCOMPARE(tracks[0].thumbnail, QString("https://i.scdn.co/300"));
        QVERIFY(qAbs(tracks[0].duration - 223.069) < 0.001);

        // Titres likés : la durée s'appelle « duration », l'uri est sur l'enveloppe.
        const auto liked = json("{'totalCount': 120, 'items': ["
                       "{'track': {'_uri': 'spotify:track:4uLU6hMCjMI75M1A2tKUQC', 'data': {'__typename': 'Track', 'name': 'Never',"
                       "'duration': {'totalMilliseconds': 213000}, 'artists': {'items': [{'profile': {'name': 'Rick'}}]},"
                       "'albumOfTrack': {'name': 'Album'}}}},"
                       "{'track': {'_uri': 'spotify:track:0000000000000000000000', 'data': {'__typename': 'NotFound'}}}]}");
        const QList<Track> likedTracks = pathfinderLikedTracks(liked, &total);
        QCOMPARE(total, 120);
        QCOMPARE(likedTracks.size(), 1);
        QCOMPARE(likedTracks[0].spotifyId, QString("4uLU6hMCjMI75M1A2tKUQC"));
        QCOMPARE(likedTracks[0].duration, 213.0);

        // Bibliothèque : playlists et « Titres likés » gardés, dossiers et albums ignorés.
        const auto library = json("{'__typename': 'LibraryPage', 'totalCount': 4, 'items': ["
                            "{'item': {'__typename': 'LibraryPseudoPlaylistResponseWrapper', '_uri': 'spotify:collection:tracks',"
                            "'data': {'__typename': 'PseudoPlaylist', 'uri': 'spotify:collection:tracks', 'name': 'Liked Songs', 'count': 42}}},"
                            "{'item': {'__typename': 'PlaylistResponseWrapper', '_uri': 'spotify:playlist:2CFaMroNgUoEvzbXdI7BIt',"
                            "'data': {'__typename': 'Playlist', 'uri': 'spotify:playlist:2CFaMroNgUoEvzbXdI7BIt', 'name': 'Été'}}},"
                            "{'item': {'__typename': 'LibraryFolderResponseWrapper', '_uri': 'spotify:user:x:folder:1',"
                            "'data': {'__typename': 'Folder', 'name': 'Dossier'}}},"
                            "{'item': {'__typename': 'AlbumResponseWrapper', '_uri': 'spotify:album:1', 'data': {'__typename': 'Album'}}}]}");
        const QList<RemotePlaylist> lib = pathfinderLibrary(library, &total);
        QCOMPARE(total, 4);
        QCOMPARE(lib.size(), 2);
        QCOMPARE(lib[0].url, QString("spotify:liked"));
        QCOMPARE(lib[0].count, 42);
        QCOMPARE(lib[1].id, QString("2CFaMroNgUoEvzbXdI7BIt"));
        QCOMPARE(lib[1].title, QString("Été"));
        QCOMPARE(lib[1].url, QString("https://open.spotify.com/playlist/2CFaMroNgUoEvzbXdI7BIt"));
        QCOMPARE(lib[1].count, -1);
    }

    void database()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath("t.db"), nullptr));
        QCOMPARE(db.playlists().size(), 1);
        QVERIFY(!db.deletePlaylist(db.cachePlaylistId()));

        // Réglages : servis depuis la mémoire, mais bien persistés en base.
        QCOMPARE(db.setting("volume", "80"), QString("80"));
        db.setSetting("volume", "55");
        db.setSetting("prefetch", "false");
        db.removeSetting("prefetch");
        {
            Database reopened;
            QVERIFY(reopened.open(dir.filePath("t.db"), nullptr));
            QCOMPARE(reopened.setting("volume"), QString("55"));
            QVERIFY(reopened.settingBool("prefetch", true));
        }
        QVERIFY(db.open(dir.filePath("t.db"), nullptr));

        // Upsert idempotent ; les métadonnées Spotify priment ensuite.
        const Track a = db.upsertTrack(input("abc", "", "ARTIST - Song (Official Video)"));
        const Track b = db.upsertTrack(input("abc", "Artist", "Song", "sp1"));
        QCOMPARE(a.id, b.id);
        QCOMPARE(b.title, QString("Song"));
        QCOMPARE(db.upsertTrack(input("abc", "Chaîne", "Autre")).title, QString("Song"));

        // Correspondance locale par id Spotify ou par clé.
        QCOMPARE(db.findLocalMatch("sp1", "", "")->id, a.id);
        QCOMPARE(db.findLocalMatch({}, "artist", "song")->id, a.id);
        QVERIFY(!db.findLocalMatch({}, "Artist", "Autre chose"));

        // Éviction : jamais un morceau présent dans une playlist personnelle.
        const Track c = db.upsertTrack(input("def", "C", "c"));
        for (const Track &t : {a, c}) {
            db.setFile(t.id, "/nonexistent", "opus", 160, 1000, 0);
            db.markStarted(t.id);
        }
        const qint64 pl = db.createPlaylist("Fav", "user");
        db.addToPlaylist(pl, a.id);
        db.addToPlaylist(pl, a.id);
        const auto cands = db.evictionCandidates();
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands.first().id, c.id);
        QVERIFY(db.isPinned(a.id));
        // Espace disque : seuls les morceaux présents en local comptent.
        db.upsertTrack(input("zzz", "Z", "pas en local"));
        db.addToPlaylist(pl, db.trackByYt("zzz")->id);
        QCOMPARE(db.playlist(pl)->diskSize, qint64(1000));
        QCOMPARE(db.playlist(db.cachePlaylistId())->diskSize, qint64(2000));
        db.removeFromPlaylist(pl, db.trackByYt("zzz")->id);

        // Recherche locale multi-mots (seulement les morceaux téléchargés).
        QCOMPARE(db.searchLocal("song artist", 10).size(), 1);
        QCOMPARE(db.searchLocal("song inconnu", 10).size(), 0);

        // Doublons : même morceau, ou autre version du même titre (même clé artiste|titre).
        const Track other = db.upsertTrack(input("ghi", "Artist", "Song (Clip officiel)"));
        QCOMPARE(db.playlistsContaining(a.id, Matcher::matchKey(a.artist, a.title)).value(pl, false), true);
        const auto similar = db.playlistsContaining(other.id, Matcher::matchKey(other.artist, other.title));
        QVERIFY(similar.contains(pl));
        QCOMPARE(similar.value(pl), false);
        QVERIFY(!db.playlistsContaining(c.id, {}).contains(pl));  // (seulement dans le cache local)
        Track pastedLink;  // lien YouTube collé : ni titre ni artiste (QString nulles)
        pastedLink.ytId = "jklmnopqrst";
        QVERIFY(db.upsertTrack(pastedLink).id > 0);

        // Réordonnancement.
        db.addToPlaylist(pl, c.id);
        db.moveInPlaylist(pl, c.id, 0);
        QCOMPARE(db.playlistTracks(pl).first().id, c.id);
    }

    void playlistFolders()
    {
        QTemporaryDir dir;
        Paths paths;
        paths.data = dir.filePath("data");
        paths.library = paths.data + "/library";
        paths.covers = paths.data + "/covers";
        paths.bin = paths.data + "/bin";
        paths.db = paths.data + "/f.db";
        for (const QString &d : {paths.library, paths.covers, paths.bin})
            QDir().mkpath(d);
        Database db;
        QVERIFY(db.open(paths.db, nullptr));
        YtDlp yt(paths.bin);
        Downloader dl(&db, &yt, paths);

        // Noms lisibles et valables partout ; « <id>.ext » dans la bibliothèque.
        Track named;
        named.ytId = "abcdefghijk";
        named.artist = "AC/DC";
        named.title = "Back In Black?  ";
        QCOMPARE(Downloader::fileNameFor(named, "opus", false), QString("AC DC - Back In Black [abcdefghijk].opus"));
        QCOMPARE(Downloader::fileNameFor(named, "opus", true), QString("abcdefghijk.opus"));

        const Track t = db.upsertTrack(input("abcdefghijk", "Artist", "Song"));
        const QString libFile = paths.library + "/abcdefghijk.opus";
        {
            QFile f(libFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("opus data");
        }
        db.setFile(t.id, libFile, "opus", 160, 9, 0);
        const QString f1 = dir.filePath("Musique/Chill"), f2 = dir.filePath("Musique/Sport");
        const qint64 p1 = db.createPlaylist("Chill", "user"), p2 = db.createPlaylist("Sport", "user");
        db.setPlaylistFolder(p1, f1);
        db.setPlaylistFolder(p2, f2);
        db.setPlaylistFolder(db.cachePlaylistId(), f1);  // ignoré : le cache n'a pas de dossier
        QVERIFY(db.playlist(db.cachePlaylistId())->folder.isEmpty());
        const QString name = "Artist - Song [abcdefghijk].opus";

        // Dans une playlist à dossier : déplacé et renommé.
        QVERIFY(db.tracksToPlace(paths.library).isEmpty());
        db.addToPlaylist(p2, t.id);
        QCOMPARE(db.tracksToPlace(paths.library), QList<qint64>({t.id}));
        QVERIFY(dl.placeFile(t.id));
        QCOMPARE(db.track(t.id)->filePath, f2 + "/" + name);
        QVERIFY(!QFile::exists(libFile));
        // Dans deux playlists à dossier : celle qui vient en premier dans la barre latérale.
        db.addToPlaylist(p1, t.id);
        QVERIFY(dl.placeFile(t.id));
        QCOMPARE(db.track(t.id)->filePath, f1 + "/" + name);
        db.setPlaylistOrder({db.cachePlaylistId(), p2, p1});
        QVERIFY(dl.placeFile(t.id));
        QCOMPARE(db.track(t.id)->filePath, f2 + "/" + name);
        // Un autre fichier porte déjà le nom voulu : jamais écrasé, le morceau ne bouge pas.
        {
            QFile other(f1 + "/" + name);
            QVERIFY(other.open(QIODevice::WriteOnly));
            other.write("fichier de l'utilisateur, à ne pas toucher");
        }
        db.removeFromPlaylist(p2, t.id);
        QVERIFY(!dl.placeFile(t.id));
        QCOMPARE(db.track(t.id)->filePath, f2 + "/" + name);
        QFile::remove(f1 + "/" + name);
        // Retiré des playlists à dossier : retour dans la bibliothèque, sous son nom d'origine.
        db.removeFromPlaylist(p1, t.id);
        QCOMPARE(db.tracksToPlace(paths.library), QList<qint64>({t.id}));  // encore hors de la bibliothèque
        QVERIFY(dl.placeFile(t.id));
        QCOMPARE(db.track(t.id)->filePath, libFile);
        QVERIFY(QFile::exists(libFile));
        QVERIFY(db.tracksToPlace(paths.library).isEmpty());

        // Rangement automatique (celui que déclenchent les changements de playlists).
        QSignalSpy placed(&dl, &Downloader::placementDone);
        db.addToPlaylist(p1, t.id);
        dl.schedulePlacement();
        dl.schedulePlacement();  // regroupé avec le précédent
        QVERIFY(placed.wait(5000));
        QCOMPARE(placed.count(), 1);
        QCOMPARE(placed.first().at(0).toInt(), 1);  // déplacés
        QCOMPARE(placed.first().at(1).toInt(), 0);  // échecs
        QCOMPARE(db.track(t.id)->filePath, f1 + "/" + name);
    }

    void playlistOrder()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("o.db");
        auto names = [](const Database &db) {
            QStringList out;
            for (const Playlist &p : db.playlists())
                out << p.name;
            return out;
        };
        {
            // Base d'une version précédente : pas de colonne sort_order.
            QSqlDatabase old = QSqlDatabase::addDatabase("QSQLITE", "old");
            old.setDatabaseName(path);
            QVERIFY(old.open());
            QSqlQuery q(old);
            QVERIFY(q.exec("CREATE TABLE playlists (id INTEGER PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL,"
                           " source_ref TEXT, created_at INTEGER NOT NULL)"));
            QVERIFY(q.exec("INSERT INTO playlists(name, kind, created_at) VALUES ('Cache local', 'cache', 0),"
                           " ('zouk', 'user', 0), ('Ambient', 'user', 0), ('Metal', 'spotify', 0)"));
            old.close();
        }
        QSqlDatabase::removeDatabase("old");

        Database db;
        QVERIFY(db.open(path, nullptr));
        // Migration : l'ordre alphabétique d'avant est conservé, « Cache local » en tête.
        QCOMPARE(names(db), QStringList({"Cache local", "Ambient", "Metal", "zouk"}));
        // Une nouvelle playlist arrive en dernier, pas à sa place alphabétique.
        db.createPlaylist("Bossa", "user");
        QCOMPARE(names(db), QStringList({"Cache local", "Ambient", "Metal", "zouk", "Bossa"}));
        // Nouvel ordre choisi (« Cache local » compris), conservé après réouverture.
        QList<qint64> ids;
        for (const Playlist &p : db.playlists())
            ids.prepend(p.id);
        db.setPlaylistOrder(ids);
        Database reopened;
        QVERIFY(reopened.open(path, nullptr));
        QCOMPARE(names(reopened), QStringList({"Bossa", "zouk", "Metal", "Ambient", "Cache local"}));
    }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
