package io.github.killex8569.orpheus;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

import com.yausername.ffmpeg.FFmpeg;
import com.yausername.youtubedl_android.YoutubeDL;

// Fonctions Android appelées depuis le C++ (src/AndroidBridge.cpp, src/AndroidMedia.cpp).
public class OrpheusAndroid {
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static WifiManager.MulticastLock multicastLock;

    // Commande de lecture venue du système (notification, écran verrouillé, casque) :
    // play | pause | toggle | next | prev. Enregistrée par setupAndroidMedia().
    public static native void nativeCommand(String command);

    static void command(String command) {
        try {
            nativeCommand(command);
        } catch (UnsatisfiedLinkError e) {
            // Orpheus n'est pas (encore) chargé dans ce processus : rien à commander.
        }
    }

    // ---------- yt-dlp (youtubedl-android) : appels bloquants, hors du thread principal ----------

    // "" si tout est prêt, sinon le message d'erreur.
    public static String initYtDlp(Context ctx) {
        try {
            YoutubeDL.getInstance().init(ctx);
            FFmpeg.getInstance().init(ctx);
            return "";
        } catch (Throwable e) {
            return String.valueOf(e);
        }
    }

    private static final String RELEASES = "https://github.com/yt-dlp/yt-dlp/releases";

    // « ok:message » ou « error:message ». Comme sur ordinateur : la dernière version est lue
    // dans la redirection de releases/latest, pas par l'API GitHub (limitée à 60 appels par
    // heure, ce que updateYoutubeDL() de youtubedl-android ne supporte pas), et yt-dlp n'est
    // téléchargé que s'il a changé. Le fichier remplacé est celui qu'utilise la bibliothèque.
    public static String updateYtDlp(Context ctx) {
        try {
            File dir = new File(ctx.getNoBackupFilesDir(), "youtubedl-android/yt-dlp");
            File target = new File(dir, "yt-dlp");
            File tagFile = new File(dir, "orpheus-version");
            HttpURLConnection c = (HttpURLConnection) new URL(RELEASES + "/latest").openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(20000);
            c.setReadTimeout(20000);
            int code = c.getResponseCode();
            String location = c.getHeaderField("Location");
            c.disconnect();
            if (location == null || !location.contains("/tag/"))
                return "error:dernière version de yt-dlp introuvable (HTTP " + code + ")";
            String tag = location.substring(location.lastIndexOf('/') + 1);
            if (target.exists() && tagFile.exists()
                && tag.equals(new String(Files.readAllBytes(tagFile.toPath()), StandardCharsets.UTF_8).trim()))
                return "ok:yt-dlp est déjà à jour";
            File part = new File(dir, "yt-dlp.part");
            try (InputStream in = new URL(RELEASES + "/download/" + tag + "/yt-dlp").openStream()) {
                Files.copy(in, part.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            // Archive Python exécutable : commence par « #! » ; sinon (page d'erreur…), on garde l'ancienne.
            byte[] head = new byte[2];
            try (InputStream in = new FileInputStream(part)) {
                if (in.read(head) != 2 || head[0] != '#' || head[1] != '!' || part.length() < 1000000) {
                    part.delete();
                    return "error:téléchargement de yt-dlp incomplet";
                }
            }
            if (!part.renameTo(target)) {
                part.delete();
                return "error:impossible de remplacer yt-dlp";
            }
            Files.write(tagFile.toPath(), tag.getBytes(StandardCharsets.UTF_8));
            return "ok:yt-dlp " + tag + " installé";
        } catch (Throwable e) {
            return "error:mise à jour de yt-dlp impossible : " + e;
        }
    }

    public static String nativeLibDir(Context ctx) {
        return ctx.getApplicationInfo().nativeLibraryDir;
    }

    public static String noBackupDir(Context ctx) {
        return ctx.getNoBackupFilesDir().getAbsolutePath();
    }

    public static String cacheDir(Context ctx) {
        return ctx.getCacheDir().getAbsolutePath();
    }

    // ---------- Jam ----------

    public static synchronized void setMulticastLock(Context ctx, boolean on) {
        if (multicastLock == null) {
            WifiManager wifi = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi == null)
                return;
            multicastLock = wifi.createMulticastLock("orpheus-jam");
            multicastLock.setReferenceCounted(false);
        }
        if (on)
            multicastLock.acquire();
        else if (multicastLock.isHeld())
            multicastLock.release();
    }

    // ---------- Lecture en arrière-plan ----------

    // Titre vide : plus rien à lire, le service et sa notification s'arrêtent.
    public static void updateMedia(final Context ctx, final String title, final String artist, final boolean playing) {
        main.post(() -> {
            OrpheusService.title = title;
            OrpheusService.artist = artist;
            OrpheusService.playing = playing;
            OrpheusService service = OrpheusService.instance;
            if (title.isEmpty()) {
                if (service != null)
                    service.finish();
            } else if (service != null) {
                service.refresh();
            } else if (playing) {
                try {
                    ctx.startForegroundService(new Intent(ctx, OrpheusService.class));
                } catch (Exception e) {
                    // Démarrage refusé en arrière-plan (Android 12+) : réessayé au prochain changement.
                }
            }
        });
    }

    public static void requestNotificationPermission(final Context ctx) {
        if (Build.VERSION.SDK_INT < 33 || !(ctx instanceof Activity))
            return;
        final Activity activity = (Activity) ctx;
        if (activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)
            return;
        main.post(() -> activity.requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1));
    }

    public static void moveTaskToBack(Context ctx) {
        if (ctx instanceof Activity) {
            final Activity activity = (Activity) ctx;
            main.post(() -> activity.moveTaskToBack(true));
        }
    }
}
