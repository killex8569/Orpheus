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
            return String.valueOf(e.getMessage());
        }
    }

    // « ok:message » ou « error:message ».
    public static String updateYtDlp(Context ctx) {
        try {
            YoutubeDL.UpdateStatus s = YoutubeDL.getInstance().updateYoutubeDL(ctx, YoutubeDL.UpdateChannel._STABLE);
            return s == YoutubeDL.UpdateStatus.ALREADY_UP_TO_DATE ? "ok:yt-dlp est déjà à jour" : "ok:yt-dlp mis à jour";
        } catch (Throwable e) {
            return "error:mise à jour de yt-dlp impossible : " + e.getMessage();
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
