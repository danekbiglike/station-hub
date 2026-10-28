package ru.stationhub.app;

import java.io.DataInputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/** Observes Yandex audio-player events locally; speech and other data are not retained. */
final class StationMediaMonitor {
    private static final int PORT = 9906, MAX_FRAME = 600000;
    private final AudioReceiverService service;
    private volatile boolean running;
    private volatile Socket socket;
    private Thread reader;
    private final Map<String, Player> players = new HashMap<>();
    private static final class Player {
        long updated, positionMs = -1, durationMs = -1;
        boolean playing, paused, mediaEligible;
        String title = "", artist = "", artwork = "", trackId = "";
    }
    StationMediaMonitor(AudioReceiverService service) { this.service = service; }
    void start() { running = true; reader = new Thread(this::listen, "station-hub-media"); reader.start(); }
    void stop() { running = false; try { if (socket != null) socket.close(); } catch (Exception ignored) {} }
    private void listen() {
        while (running) {
            try (Socket s = new Socket()) {
                socket = s; s.connect(new InetSocketAddress("127.0.0.1", PORT), 1000); s.setSoTimeout(3000);
                DataInputStream in = new DataInputStream(s.getInputStream());
                while (running) {
                    int n;
                    try { n = Integer.reverseBytes(in.readInt()); }
                    catch (java.net.SocketTimeoutException timeout) { expire(); continue; }
                    if (n <= 0 || n > MAX_FRAME) break;
                    byte[] data = new byte[n]; in.readFully(data);
                    try { process(data); } catch (RuntimeException ignored) {}
                    expire();
                }
            } catch (Exception ignored) {
                // The local media service may restart independently of this app.
            } finally {
                socket = null; players.clear(); service.setAliceState(false, false, "", "", "", "", -1, -1, 0);
            }
            if (running) try { Thread.sleep(1500); } catch (InterruptedException ignored) {}
        }
    }
    private void process(byte[] data) {
        VoiceRouter.Value kind = VoiceRouter.find(data, 0, data.length, 253, 0);
        VoiceRouter.Value body = kind != null && kind.number == 127 ? VoiceRouter.find(data, 0, data.length, 252, 2)
                : VoiceRouter.find(data, 0, data.length, 127, 2);
        if (body == null) return;
        int end = body.offset + body.length;
        VoiceRouter.Value descriptor = VoiceRouter.find(data, body.offset, end, 4, 2);
        VoiceRouter.Value state = VoiceRouter.find(data, body.offset, end, 2, 0);
        if (descriptor == null || state == null) return;
        int descriptorEnd = descriptor.offset + descriptor.length;
        VoiceRouter.Value type = VoiceRouter.find(data, descriptor.offset, descriptorEnd, 1, 0);
        VoiceRouter.Value id = VoiceRouter.find(data, descriptor.offset, descriptorEnd, 2, 2);
        if (type == null || type.number != 1 || id == null) return;
        String playerId = id.text(data);
        Player player = players.get(playerId);
        if (player == null) {
            if (players.size() >= 32) {
                String oldest = null; long time = Long.MAX_VALUE;
                for (Map.Entry<String, Player> item : players.entrySet()) if (item.getValue().updated < time) {
                    time = item.getValue().updated; oldest = item.getKey();
                }
                if (oldest != null) players.remove(oldest);
            }
            player = new Player(); players.put(playerId, player);
        }
        player.updated = android.os.SystemClock.elapsedRealtime();
        VoiceRouter.Value audio = VoiceRouter.find(data, body.offset, end, 3, 2);
        if (audio != null) {
            int audioEnd = audio.offset + audio.length;
            VoiceRouter.Value audioId = VoiceRouter.find(data, audio.offset, audioEnd, 1, 2);
            VoiceRouter.Value audioType = VoiceRouter.find(data, audio.offset, audioEnd, 20, 2);
            if (audioId != null) {
                String value=audioId.text(data);
                player.trackId=value.matches("[0-9]{1,18}") && (audioType == null || "Track".equals(audioType.text(data))) ? value : "";
            }
            if (audioType != null && !"Track".equals(audioType.text(data))) player.trackId="";
            VoiceRouter.Value position = VoiceRouter.find(data, audio.offset, audioEnd, 28, 0);
            VoiceRouter.Value duration = VoiceRouter.find(data, audio.offset, audioEnd, 6, 0);
            if (position != null && position.number >= 0) player.positionMs = position.number;
            if (duration != null && duration.number > 0 && duration.number < 604800) player.durationMs = duration.number * 1000;
            VoiceRouter.Value screen = VoiceRouter.find(data, audio.offset, audioEnd, 11, 0);
            if (screen != null) player.mediaEligible = screen.number == 1 || screen.number == 2;
            VoiceRouter.Value metadata = VoiceRouter.find(data, audio.offset, audioEnd, 9, 2);
            if (metadata != null) {
                int metadataEnd = metadata.offset + metadata.length;
                VoiceRouter.Value title = VoiceRouter.find(data, metadata.offset, metadataEnd, 1, 2);
                VoiceRouter.Value artist = VoiceRouter.find(data, metadata.offset, metadataEnd, 2, 2);
                VoiceRouter.Value artwork = VoiceRouter.find(data, metadata.offset, metadataEnd, 3, 2);
                String nextTitle=title == null ? "" : title.text(data);
                String nextArtist=artist == null ? "" : artist.text(data);
                // Never carry the previous track's rating identity into new metadata.
                if (audioId == null && (!player.title.equals(nextTitle) || !player.artist.equals(nextArtist))) player.trackId="";
                player.title = nextTitle;
                player.artist = nextArtist;
                player.artwork = artwork == null ? "" : artwork.text(data);
            }
        }
        player.playing = state.number == 3 && player.mediaEligible;
        player.paused = state.number == 4 && player.mediaEligible;
        update();
    }
    private void expire() {
        long now = android.os.SystemClock.elapsedRealtime();
        boolean changed = false;
        Iterator<Player> it = players.values().iterator();
        while (it.hasNext()) { Player player = it.next();
            long maxAge = player.paused ? 86400000 : 6000;
            if (now - player.updated > maxAge) { it.remove(); changed = true; }
        }
        if (changed) update();
    }
    private void update() {
        Player active = null, latest = null;
        for (Player p : players.values()) {
            if (p.playing && (active == null || p.updated > active.updated)) active = p;
            if (p.paused && (latest == null || p.updated > latest.updated)) latest = p;
        }
        Player shown = active != null ? active : latest;
        service.setAliceState(true, active != null, shown == null ? "" : shown.title, shown == null ? "" : shown.artist,
                shown == null ? "" : shown.artwork, shown == null ? "" : shown.trackId, shown == null ? -1 : shown.positionMs,
                shown == null ? -1 : shown.durationMs, shown == null ? 0 : shown.updated);
    }
}
