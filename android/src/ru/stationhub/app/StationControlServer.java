package ru.stationhub.app;

import android.content.Context;
import android.os.SystemClock;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.security.MessageDigest;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import org.json.JSONObject;

/** Authenticated LAN control, independent of the PC audio stream. */
final class StationControlServer {
    private final AudioReceiverService service;
    private final ExecutorService clients = new ThreadPoolExecutor(3, 3, 0, TimeUnit.SECONDS, new ArrayBlockingQueue<>(8));
    private volatile boolean running;
    private volatile ServerSocket server;
    private long lastRestartAt;
    private String lastRestartTrack = "";
    StationControlServer(AudioReceiverService service) { this.service = service; }
    void start() { running = true; new Thread(this::listen, "station-hub-control").start(); }
    void stop() { running = false; try { if (server != null) server.close(); } catch (Exception ignored) {} clients.shutdownNow(); }
    private void listen() {
        try (ServerSocket listener = new ServerSocket()) {
            server = listener; listener.bind(new InetSocketAddress(27363), 8);
            while (running) { Socket client = listener.accept();
                try { clients.execute(() -> handle(client)); }
                catch (RuntimeException rejected) { try { client.close(); } catch (Exception ignored) {} }
            }
        } catch (Exception ignored) {} finally { server = null; }
    }
    private void handle(Socket socket) {
        try (Socket s = socket) {
            InetAddress addr = s.getInetAddress();
            if (!(addr.isSiteLocalAddress() || addr.isLinkLocalAddress() || addr.isLoopbackAddress())) return;
            s.setSoTimeout(2500); InputStream in = s.getInputStream(); byte[] line = new byte[128]; int n=0, b;
            while (n < line.length && (b=in.read()) >= 0 && b != '\n') line[n++]=(byte)b;
            if (n == line.length) return;
            String request = new String(line, 0, n, StandardCharsets.US_ASCII); int split=request.indexOf(' ');
            if (split != 16 || !MessageDigest.isEqual(request.substring(0,split).getBytes(StandardCharsets.US_ASCII), Pairing.getKey(service).getBytes(StandardCharsets.US_ASCII))) return;
            JSONObject reply = execute(request.substring(split+1));
            OutputStream out = s.getOutputStream(); out.write((reply.toString()+"\n").getBytes(StandardCharsets.UTF_8)); out.flush();
        } catch (Exception ignored) {}
    }
    private synchronized JSONObject execute(String command) throws Exception {
        if ("STATE".equals(command)) {
            long pos=AudioReceiverService.alicePositionMs;
            if (pos>=0 && AudioReceiverService.alicePlaying && AudioReceiverService.alicePositionAt>0)
                pos+=Math.min(5000,Math.max(0,SystemClock.elapsedRealtime()-AudioReceiverService.alicePositionAt));
            long duration=AudioReceiverService.aliceDurationMs; if (duration>0 && pos>duration) pos=duration;
            return new JSONObject().put("result","OK").put("known",AudioReceiverService.aliceKnown)
                    .put("has_media",AudioReceiverService.aliceHasMedia).put("playing",AudioReceiverService.alicePlaying)
                    .put("title",AudioReceiverService.aliceTitle).put("artist",AudioReceiverService.aliceArtist)
                    .put("artwork",AudioReceiverService.aliceArtwork).put("position_ms",pos).put("duration_ms",duration)
                    .put("track_token",trackToken())
                    .put("rating",AudioReceiverService.aliceTrackId.equals(AudioReceiverService.aliceRatingTrackId) ? AudioReceiverService.aliceRating : "UNKNOWN")
                    .put("rating_status",AudioReceiverService.aliceRatingStatus)
                    .put("volume",AudioReceiverService.stationVolume);
        }
        if (command.startsWith("VOLUME_SET ")) {
            String levelText=command.substring("VOLUME_SET ".length());
            if (!levelText.matches("(?:[1-9]|10)")) return new JSONObject().put("result","UNAVAILABLE");
            int current=AudioReceiverService.stationVolume, target=Integer.parseInt(levelText);
            if (current<0 || current>10) return new JSONObject().put("result","UNAVAILABLE");
            // Do not allow a single action to make a sudden large increase.
            if (target>current+1) return new JSONObject().put("result","VOLUME_LIMIT");
            if (target==current) return new JSONObject().put("result","SENT");
            return new JSONObject().put("result",VoiceRouter.sendStationVolume(target) ? "SENT" : "FAILED");
        }
        if ("VOLUME_UP".equals(command) || "VOLUME_DOWN".equals(command)) {
            // Disabled until a safe Station-specific mapping is verified.
            return new JSONObject().put("result","UNAVAILABLE");
        }
        if (command.startsWith("LIKE ") || command.startsWith("DISLIKE ")) {
            if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia || !AudioReceiverService.alicePlaying)
                return new JSONObject().put("result","NO_MEDIA");
            String[] parts=command.split(" ", -1);
            if (parts.length!=2 || !parts[1].equals(trackToken())) return new JSONObject().put("result","UNAVAILABLE");
            boolean sent=VoiceRouter.sendStationRating(parts[0].equals("DISLIKE"));
            if (sent) service.refreshStationRating(AudioReceiverService.aliceTrackId);
            return new JSONObject().put("result",sent ? "SENT" : "FAILED");
        }
        if (command.startsWith("UNLIKE ") || command.startsWith("UNDISLIKE ")) {
            String[] parts=command.split(" ", -1); String id=AudioReceiverService.aliceTrackId;
            if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia || !id.matches("[0-9]{1,18}")) return new JSONObject().put("result","NO_MEDIA");
            if (parts.length!=2 || !parts[1].equals(trackToken()) || !id.equals(AudioReceiverService.aliceRatingTrackId)) return new JSONObject().put("result","UNAVAILABLE");
            String expected=parts[0].equals("UNLIKE") ? "LIKE" : "DISLIKE";
            if (!expected.equals(AudioReceiverService.aliceRating)) return new JSONObject().put("result","UNAVAILABLE");
            return new JSONObject().put("result",service.removeStationRating(parts[0].equals("UNLIKE") ? "REMOVE_LIKE" : "REMOVE_DISLIKE",id) ? "QUEUED" : "FAILED");
        }
        if (command.startsWith("SEEK ")) {
            if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia) return new JSONObject().put("result","NO_MEDIA");
            String[] parts=command.split(" ", -1);
            if (parts.length!=3 || !parts[1].matches("[0-9]{1,9}") || !parts[2].equals(trackToken())) return new JSONObject().put("result","UNAVAILABLE");
            long target=Long.parseLong(parts[1]);
            long duration=AudioReceiverService.aliceDurationMs;
            if (duration<=0 || target>duration) return new JSONObject().put("result","UNAVAILABLE");
            byte[] body={0x6a,0x0b,0x08,0x02,0x11,0,0,0,0,0,0,0,0};
            ByteBuffer.wrap(body,5,8).order(ByteOrder.LITTLE_ENDIAN).putDouble(target / 1000.0);
            lastRestartAt=0;
            return new JSONObject().put("result",sendAudioRequest(body) ? "SENT" : "FAILED");
        }
        int action;
        switch (command) {
            case "NEXT": lastRestartAt=0; action=6; break;
            case "PREV":
                if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia) return new JSONObject().put("result","NO_MEDIA");
                long position=AudioReceiverService.alicePositionMs;
                long now=SystemClock.elapsedRealtime();
                if (position < 0) return new JSONObject().put("result","UNAVAILABLE");
                if (AudioReceiverService.alicePlaying && AudioReceiverService.alicePositionAt>0)
                    position+=Math.min(5000,Math.max(0,now-AudioReceiverService.alicePositionAt));
                String track=AudioReceiverService.aliceTitle+"\n"+AudioReceiverService.aliceArtist+"\n"+AudioReceiverService.aliceArtwork;
                // The monitor may take a moment to report the rewind; a quick second press
                // must still reach the previous track instead of repeating the seek.
                if (position < 10000 || (lastRestartAt>0 && now-lastRestartAt<3000 && track.equals(lastRestartTrack))) {
                    lastRestartAt=0;
                    return new JSONObject().put("result",VoiceRouter.sendStationAction(7) ? "SENT" : "FAILED");
                }
                boolean restarted=sendAudioRequest(new byte[]{0x6a,0x0b,0x08,0x02,0x11,0,0,0,0,0,0,0,0});
                if (restarted) { lastRestartAt=now; lastRestartTrack=track; }
                return new JSONObject().put("result",restarted ? "SENT" : "FAILED");
            case "PLAY":
                if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia) return new JSONObject().put("result","NO_MEDIA");
                return new JSONObject().put("result",setStationPaused(false) ? "SENT" : "FAILED");
            case "PAUSE":
                if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia) return new JSONObject().put("result","NO_MEDIA");
                return new JSONObject().put("result",setStationPaused(true) ? "SENT" : "FAILED");
            case "LIKE": case "DISLIKE": case "UNLIKE": case "UNDISLIKE":
                return new JSONObject().put("result","UNAVAILABLE");
            default: return new JSONObject().put("result","UNKNOWN");
        }
        if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia) return new JSONObject().put("result","NO_MEDIA");
        return new JSONObject().put("result",VoiceRouter.sendStationAction(action) ? "SENT" : "FAILED");
    }
    private static String trackToken() throws Exception {
        String track=AudioReceiverService.aliceTrackId+"\n"+AudioReceiverService.aliceTitle+"\n"+AudioReceiverService.aliceArtist+"\n"+AudioReceiverService.aliceArtwork+"\n"+AudioReceiverService.aliceDurationMs;
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(track.getBytes(StandardCharsets.UTF_8));
        StringBuilder result=new StringBuilder();
        for (int i=0;i<8;i++) result.append(String.format(java.util.Locale.ROOT,"%02x",digest[i]&255));
        return result.toString();
    }
    private static boolean setStationPaused(boolean paused) {
        // AudioClient MediaRequest.pause or resume=AUDIO; these are separate from generic capabilities.
        return sendAudioRequest(new byte[]{paused ? (byte)0x22 : (byte)0x28, paused ? (byte)0 : (byte)1});
    }
    private static boolean sendAudioRequest(byte[] body) {
        // Small MediaRequest body wrapped in a length-prefixed Quasar message.
        int size=body.length+6;
        byte[] frame=new byte[size+4];
        frame[0]=(byte)size; frame[1]=(byte)(size>>>8); frame[2]=(byte)(size>>>16); frame[3]=(byte)(size>>>24);
        frame[4]=(byte)0xe2; frame[5]=0x0f; frame[6]=(byte)body.length;
        System.arraycopy(body,0,frame,7,body.length);
        frame[7+body.length]=(byte)0xe8; frame[8+body.length]=0x0f; frame[9+body.length]=8;
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1",9906),1000);
            s.getOutputStream().write(frame); s.getOutputStream().flush(); Thread.sleep(250); return true;
        } catch (Exception ignored) { return false; }
    }
}
