package ru.stationhub.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.DataOutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import org.json.JSONObject;

public final class AudioReceiverService extends Service {
    public static final int PORT = 27361;
    public static final String ACTION_STATE = "ru.stationhub.app.STATE";
    private static final int MAGIC = 0x53485531; // SHU1, network byte order
    private static final int MAGIC_WITH_MODE = 0x53485532; // SHU2
    private volatile boolean running;
    private volatile ServerSocket server;
    private volatile Socket client;
    private Thread worker;
    private VoiceRouter voiceRouter;
    private StationMediaMonitor stationMediaMonitor;
    private StationVolumeMonitor stationVolumeMonitor;
    private StationControlServer stationControlServer;
    private StationRatingMonitor stationRatingMonitor;
    private HubBridgeServer hubBridgeServer;
    private volatile boolean bridgeActive;
    private final Object likeLock = new Object();
    private boolean bridgeLikePending;
    private String bridgeLikeResult;
    private final ExecutorService artworkExecutor = Executors.newSingleThreadExecutor();
    private final AtomicInteger artworkVersion = new AtomicInteger();
    public static volatile String currentState = "waiting";
    public static volatile String currentDevice = "";
    public static volatile String currentTitle = "";
    public static volatile String currentArtist = "";
    public static volatile String currentMode = "quality";
    public static volatile boolean pcPlaying = false;
    public static volatile boolean pcNextEnabled = false;
    public static volatile boolean aliceKnown = false, alicePlaying = false;
    public static volatile boolean aliceHasMedia = false;
    public static volatile int stationVolume = -1;
    public static volatile String aliceTitle = "", aliceArtist = "";
    public static volatile String aliceArtwork = "";
    public static volatile String aliceTrackId = "", aliceRatingTrackId = "", aliceRating = "UNKNOWN", aliceRatingStatus = "";
    public static volatile long alicePositionMs = -1, aliceDurationMs = -1, alicePositionAt = 0;
    public static volatile long pcPositionMs = -1, pcDurationMs = -1, pcPositionAt = 0;
    public static volatile Bitmap pcArtwork = null;
    public static volatile String ambiguityPolicy = "station";

    public static void start(Context context) {
        Intent intent = new Intent(context, AudioReceiverService.class);
        if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(intent);
        else context.startService(intent);
    }

    @Override public void onCreate() {
        super.onCreate();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel("audio", "Station Hub", NotificationManager.IMPORTANCE_LOW);
            getSystemService(NotificationManager.class).createNotificationChannel(channel);
        }
        Intent open = new Intent(this, MainActivity.class);
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, "audio") : new Notification.Builder(this);
        startForeground(71, builder.setSmallIcon(android.R.drawable.ic_media_play).setContentTitle("Station Hub").setContentText("Приём звука по локальной сети").setContentIntent(pending).build());
        running = true;
        worker = new Thread(this::acceptLoop, "station-hub-audio");
        worker.start();
        voiceRouter = new VoiceRouter(this);
        voiceRouter.start();
        stationMediaMonitor = new StationMediaMonitor(this);
        stationMediaMonitor.start();
        stationRatingMonitor = new StationRatingMonitor(this);
        stationRatingMonitor.start();
        stationVolumeMonitor = new StationVolumeMonitor();
        stationVolumeMonitor.start();
        stationControlServer = new StationControlServer(this);
        stationControlServer.start();
        hubBridgeServer = new HubBridgeServer(this);
        hubBridgeServer.start();
        BridgeBootstrap.startIfInstalled();
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) { return START_STICKY; }
    @Override public IBinder onBind(Intent intent) { return null; }

    private void status(String state, String device) {
        currentState = state;
        currentDevice = device;
        if ("waiting".equals(state)) {
            currentTitle = ""; currentArtist = ""; pcPlaying = false; pcNextEnabled = false;
            pcPositionMs = pcDurationMs = -1; pcPositionAt = 0; pcArtwork = null; artworkVersion.incrementAndGet();
        }
        Intent i = new Intent(ACTION_STATE).setPackage(getPackageName()).putExtra("state", state).putExtra("device", device)
                .putExtra("title", currentTitle).putExtra("artist", currentArtist);
        sendBroadcast(i);
        if ("connected".equals(state) && "waiting".equals(previousState)) {
            Intent screen = new Intent(this, MainActivity.class)
                    .putExtra("stream_view", true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            try { startActivity(screen); } catch (RuntimeException ignored) {}
        }
        previousState = state;
    }

    private String previousState = "waiting";

    synchronized void setAliceState(boolean known, boolean playing, String title, String artist, String artwork, String trackId,
                                    long positionMs, long durationMs, long positionAt) {
        boolean changed = aliceKnown != known || alicePlaying != playing || !aliceTitle.equals(title)
                || !aliceArtist.equals(artist) || !aliceArtwork.equals(artwork) || !aliceTrackId.equals(trackId);
        if (!aliceTrackId.equals(trackId)) aliceRatingStatus="";
        aliceKnown = known; alicePlaying = playing; aliceTitle = title; aliceArtist = artist; aliceArtwork = artwork; aliceTrackId=trackId;
        aliceHasMedia = known && (!title.isEmpty() || !artist.isEmpty() || durationMs > 0);
        alicePositionMs = positionMs; aliceDurationMs = durationMs; alicePositionAt = positionAt;
        if (changed) sendBroadcast(new Intent(ACTION_STATE).setPackage(getPackageName()).putExtra("state", currentState).putExtra("device", currentDevice));
    }

    boolean removeStationRating(String action, String id) {
        return stationRatingMonitor != null && stationRatingMonitor.remove(action, id);
    }
    void refreshStationRating(String id) {
        if (stationRatingMonitor != null) stationRatingMonitor.refresh(id);
    }

    synchronized boolean sendControl(String command) {
        Socket destination = client;
        if (destination == null || !("playing".equals(currentState) || "connected".equals(currentState))) return false;
        try {
            byte[] payload = command.getBytes(StandardCharsets.US_ASCII);
            DataOutputStream out = new DataOutputStream(destination.getOutputStream());
            out.writeInt(payload.length); out.write(payload); out.flush();
            return true;
        } catch (Exception e) { return false; }
    }

    synchronized void setBridgeActive(boolean active) {
        if (bridgeActive == active) return;
        bridgeActive = active;
        sendControl(active ? "BRIDGE:ON" : "BRIDGE:OFF");
    }

    String bridgeLike() {
        synchronized (likeLock) {
            if (bridgeLikePending || !pcPlaying) return "FAILED";
            bridgeLikePending = true;
            bridgeLikeResult = null;
            if (!sendControl("MEDIA:LIKE")) { bridgeLikePending = false; return "FAILED"; }
            long until = android.os.SystemClock.elapsedRealtime() + 4000;
            while (bridgeLikePending) {
                long remaining = until - android.os.SystemClock.elapsedRealtime();
                if (remaining <= 0) break;
                try { likeLock.wait(remaining); } catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
            }
            bridgeLikePending = false;
            return bridgeLikeResult == null ? "FAILED" : bridgeLikeResult.substring(5);
        }
    }

    private void acceptLoop() {
        try (ServerSocket socket = new ServerSocket()) {
            socket.setReceiveBufferSize(16384);
            socket.bind(new InetSocketAddress(PORT));
            server = socket;
            while (running) {
                try (Socket s = socket.accept()) {
                    client = s;
                    s.setSoTimeout(10000);
                    receive(s);
                } catch (Exception e) {
                    if (running) status("waiting", "");
                } finally {
                    client = null;
                    status("waiting", "");
                }
            }
        } catch (Exception e) {
            if (running) status("error", "Не удалось запустить приёмник");
        } finally {
            server = null;
        }
    }

    private void receive(Socket socket) throws Exception {
        InetAddress address = socket.getInetAddress();
        if (!(address.isSiteLocalAddress() || address.isLinkLocalAddress() || address.isLoopbackAddress())) return;
        DataInputStream in = new DataInputStream(new BufferedInputStream(socket.getInputStream(), 32768));
        int magic = in.readInt();
        if (magic != MAGIC && magic != MAGIC_WITH_MODE) return;
        byte[] supplied = new byte[16];
        in.readFully(supplied);
        if (!MessageDigest.isEqual(supplied, Pairing.getKey(this).getBytes(StandardCharsets.US_ASCII))) return;
        int sampleRate = in.readInt();
        int channels = in.readInt();
        if (sampleRate < 8000 || sampleRate > 96000 || (channels != 1 && channels != 2)) return;
        int mode = magic == MAGIC_WITH_MODE ? in.readInt() : 0;
        if (mode != 0 && mode != 1) return;
        boolean game = mode == 1;
        currentMode = game ? "game" : "quality";
        int layout = channels == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
        int minimum = AudioTrack.getMinBufferSize(sampleRate, layout, AudioFormat.ENCODING_PCM_16BIT);
        if (minimum <= 0) throw new IllegalStateException("Unsupported audio format");
        // These values trade interruption resistance for latency. Android's
        // minimum is device-dependent; changing them requires real-device tests.
        int bufferBytes = game ? Math.max(minimum, sampleRate * channels / 8)
                : Math.max(minimum * 2, sampleRate * channels / 2);
        int prefillBytes = game ? sampleRate * channels / 20 : sampleRate * channels / 5;
        AudioTrack track = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate, layout,
                AudioFormat.ENCODING_PCM_16BIT, bufferBytes, AudioTrack.MODE_STREAM);
        WifiManager.WifiLock wifiLock = null;
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "StationHub:stream");
                wifiLock.acquire();
            }
        } catch (RuntimeException ignored) {
            wifiLock = null; // Continue streaming on devices without this capability.
        }
        try {
            if (track.getState() != AudioTrack.STATE_INITIALIZED) throw new IllegalStateException("AudioTrack unavailable");
            status("connected", "Компьютер");
            sendControl(bridgeActive ? "BRIDGE:ON" : "BRIDGE:OFF");
            byte[] pcm = new byte[65536];
            long lastAudible = 0;
            boolean audible = false;
            int prefilled = 0;
            boolean outputStarted = false;
            // Each big-endian length precedes either PCM or a typed metadata
            // frame. All lengths are bounded before allocation or parsing.
            while (running) {
                int length;
                try { length = in.readInt(); } catch (EOFException e) { break; }
                if (length == -1) {
                    int metadataLength = in.readInt();
                    if (metadataLength < 0 || metadataLength > 4096) throw new IllegalArgumentException("Invalid metadata");
                    byte[] metadataBytes = new byte[metadataLength];
                    in.readFully(metadataBytes);
                    JSONObject metadata = new JSONObject(new String(metadataBytes, StandardCharsets.UTF_8));
                    currentTitle = metadata.optString("title", "");
                    currentArtist = metadata.optString("artist", "");
                    pcPlaying = metadata.optBoolean("playing", false);
                    pcNextEnabled = metadata.optBoolean("next_enabled", false);
                    pcPositionMs = metadata.optLong("position_ms", -1);
                    pcDurationMs = metadata.optLong("duration_ms", -1);
                    pcPositionAt = android.os.SystemClock.elapsedRealtime();
                    String preference = metadata.optString("ambiguity", "station");
                    ambiguityPolicy = preference.equals("pc") || preference.equals("alice") || preference.equals("ask") ? preference : "station";
                    status(audible ? "playing" : "connected", "Компьютер");
                    continue;
                }
                if (length == -3) {
                    int size = in.readInt();
                    if (size < 0 || size > 131072) throw new IllegalArgumentException("Invalid artwork");
                    byte[] encoded = new byte[size]; in.readFully(encoded);
                    int version = artworkVersion.incrementAndGet();
                    artworkExecutor.execute(() -> {
                        Bitmap decoded = null;
                        if (encoded.length > 0) {
                            try {
                                BitmapFactory.Options options = new BitmapFactory.Options(); options.inJustDecodeBounds = true;
                                BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
                                if (options.outWidth > 0 && options.outHeight > 0) {
                                    options.inJustDecodeBounds = false; options.inSampleSize = 1;
                                    while (options.outWidth / options.inSampleSize > 512 || options.outHeight / options.inSampleSize > 512) options.inSampleSize *= 2;
                                    decoded = BitmapFactory.decodeByteArray(encoded, 0, encoded.length, options);
                                }
                            } catch (RuntimeException ignored) {}
                        }
                        if (artworkVersion.get() == version && running) {
                            pcArtwork = decoded;
                            sendBroadcast(new Intent(ACTION_STATE).setPackage(getPackageName()).putExtra("state", currentState).putExtra("device", currentDevice));
                        }
                    });
                    continue;
                }
                if (length == -2) {
                    int size = in.readInt();
                    if (size <= 0 || size > 128) throw new IllegalArgumentException("Invalid control response");
                    byte[] response = new byte[size]; in.readFully(response);
                    String result = new String(response, StandardCharsets.US_ASCII);
                    boolean handled;
                    synchronized (likeLock) {
                        handled = bridgeLikePending && result.startsWith("LIKE:");
                        if (handled) { bridgeLikeResult = result; bridgeLikePending = false; likeLock.notifyAll(); }
                    }
                    if (!handled && voiceRouter != null) voiceRouter.onControlResult(result);
                    continue;
                }
                if (length <= 0 || length > pcm.length || length % (channels * 2) != 0) throw new IllegalArgumentException("Invalid frame");
                in.readFully(pcm, 0, length);
                boolean signal = false;
                for (int i = 0; i + 1 < length; i += 16) {
                    int value = (short) ((pcm[i] & 0xff) | (pcm[i + 1] << 8));
                    if (Math.abs(value) > 256) { signal = true; break; }
                }
                long now = android.os.SystemClock.elapsedRealtime();
                if (signal) lastAudible = now;
                boolean next = lastAudible != 0 && now - lastAudible < 3000;
                if (next != audible) {
                    audible = next;
                    status(audible ? "playing" : "connected", "Компьютер");
                }
                int offset = 0;
                while (offset < length && running) {
                    int n = track.write(pcm, offset, length - offset);
                    if (n <= 0) throw new IllegalStateException("Audio output failed");
                    offset += n;
                    prefilled += n;
                }
                if (!outputStarted && prefilled >= prefillBytes) {
                    track.play();
                    outputStarted = true;
                }
            }
        } finally {
            try { track.stop(); } catch (IllegalStateException ignored) {}
            track.release();
            if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        }
    }

    @Override public void onDestroy() {
        running = false;
        if (hubBridgeServer != null) hubBridgeServer.stop();
        if (voiceRouter != null) voiceRouter.stop();
        if (stationMediaMonitor != null) stationMediaMonitor.stop();
        if (stationVolumeMonitor != null) stationVolumeMonitor.stop();
        if (stationControlServer != null) stationControlServer.stop();
        if (stationRatingMonitor != null) stationRatingMonitor.stop();
        artworkVersion.incrementAndGet(); artworkExecutor.shutdownNow();
        try { if (client != null) client.close(); } catch (Exception ignored) {}
        try { if (server != null) server.close(); } catch (Exception ignored) {}
        if (worker != null) worker.interrupt();
        super.onDestroy();
    }
}
