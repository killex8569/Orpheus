#include <QTemporaryDir>
#include <QTest>

#include <QTimer>

#include "Database.h"
#include "Importer.h"
#include "Lyrics.h"
#include "Matcher.h"
#include "Spotify.h"
#include "Track.h"

using namespace Matcher;

class CoreTest : public QObject {
    Q_OBJECT

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
};

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
