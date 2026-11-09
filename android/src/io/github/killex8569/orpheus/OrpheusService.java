package io.github.killex8569.orpheus;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

// Service au premier plan pendant la lecture : Android ne coupe pas Orpheus en arrière-plan,
// et la notification (comme l'écran verrouillé et les boutons du casque) pilote la lecture.
// État et commandes passent par OrpheusAndroid, toujours sur le thread principal d'Android.
public class OrpheusService extends Service {
    static OrpheusService instance;
    static String title = "";
    static String artist = "";
    static boolean playing;

    private static final String CHANNEL = "playback";
    private static final int NOTIFICATION_ID = 1;

    private MediaSession session;
    private PowerManager.WakeLock wakeLock;

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, "Lecture", NotificationManager.IMPORTANCE_LOW));
        session = new MediaSession(this, "Orpheus");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { OrpheusAndroid.command("play"); }
            @Override public void onPause() { OrpheusAndroid.command("pause"); }
            @Override public void onStop() { OrpheusAndroid.command("pause"); }
            @Override public void onSkipToNext() { OrpheusAndroid.command("next"); }
            @Override public void onSkipToPrevious() { OrpheusAndroid.command("prev"); }
        });
        session.setActive(true);
        // Téléchargements et lecture continuent écran éteint.
        wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "orpheus:playback");
        wakeLock.setReferenceCounted(false);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Boutons de la notification : l'action de l'intention est la commande.
        if (intent != null && intent.getAction() != null)
            OrpheusAndroid.command(intent.getAction());
        refresh();
        return START_NOT_STICKY;
    }

    void refresh() {
        session.setMetadata(new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, artist)
            .build());
        session.setPlaybackState(new PlaybackState.Builder()
            .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE
                        | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS)
            .setState(playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED,
                      PlaybackState.PLAYBACK_POSITION_UNKNOWN, 1f)
            .build());

        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        Notification notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(playing ? android.R.drawable.ic_media_play : android.R.drawable.ic_media_pause)
            .setContentTitle(title)
            .setContentText(artist)
            .setContentIntent(open == null ? null
                : PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT))
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setOngoing(playing)
            .addAction(action(android.R.drawable.ic_media_previous, "Précédent", "prev", 1))
            .addAction(playing ? action(android.R.drawable.ic_media_pause, "Pause", "pause", 2)
                               : action(android.R.drawable.ic_media_play, "Lecture", "play", 2))
            .addAction(action(android.R.drawable.ic_media_next, "Suivant", "next", 3))
            .setStyle(new Notification.MediaStyle()
                .setMediaSession(session.getSessionToken())
                .setShowActionsInCompactView(0, 1, 2))
            .build();
        if (Build.VERSION.SDK_INT >= 29)
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else
            startForeground(NOTIFICATION_ID, notification);
        if (playing)
            wakeLock.acquire();
        else if (wakeLock.isHeld())
            wakeLock.release();
    }

    private Notification.Action action(int icon, String label, String command, int requestCode) {
        Intent intent = new Intent(this, OrpheusService.class).setAction(command);
        PendingIntent pending = PendingIntent.getService(this, requestCode, intent,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Action.Builder(Icon.createWithResource(this, icon), label, pending).build();
    }

    void finish() {
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override
    public void onDestroy() {
        instance = null;
        if (wakeLock.isHeld())
            wakeLock.release();
        session.release();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
