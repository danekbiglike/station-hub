package ru.stationhub.app;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import org.json.JSONObject;

/** Local, authenticated interface for the optional voice bridge. */
final class HubBridgeServer {
    private final AudioReceiverService service;
    private volatile boolean running;
    private volatile ServerSocket server;
    private volatile long lastHeartbeat;
    HubBridgeServer(AudioReceiverService service) { this.service = service; }
    void start() { running = true; new Thread(this::run, "station-hub-bridge").start();
        new Thread(() -> { while (running) { long age = android.os.SystemClock.elapsedRealtime() - lastHeartbeat;
            if (lastHeartbeat != 0 && age > 10000) { lastHeartbeat = 0; service.setBridgeActive(false); }
            try { Thread.sleep(3000); } catch (InterruptedException ignored) {} } }, "station-hub-bridge-watch").start(); }
    void stop() { running = false; try { if (server != null) server.close(); } catch (Exception ignored) {} }
    private void run() {
        try (ServerSocket listener = new ServerSocket()) {
            server = listener; listener.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 27362), 4);
            while (running) try (Socket client = listener.accept()) {
                client.setSoTimeout(5000);
                InputStream in = client.getInputStream(); byte[] buf = new byte[128]; int n=0, b;
                while (n < buf.length && (b=in.read()) >= 0 && b != '\n') buf[n++]=(byte)b;
                if (n == buf.length) continue;
                String line = new String(buf, 0, n, StandardCharsets.US_ASCII);
                int split = line.indexOf(' '); if (split != 16) continue;
                byte[] supplied = line.substring(0, split).getBytes(StandardCharsets.US_ASCII);
                byte[] expected = Pairing.getKey(service).getBytes(StandardCharsets.US_ASCII);
                if (!MessageDigest.isEqual(supplied, expected)) continue;
                String answer = execute(line.substring(split + 1));
                OutputStream out = client.getOutputStream(); out.write((answer + "\n").getBytes(StandardCharsets.UTF_8)); out.flush();
            } catch (Exception ignored) {}
        } catch (Exception ignored) {} finally { server = null; }
    }
    private String execute(String command) throws Exception {
        switch (command) {
        case "PING": lastHeartbeat = android.os.SystemClock.elapsedRealtime(); service.setBridgeActive(true); return "OK";
        case "INACTIVE": lastHeartbeat = 0; service.setBridgeActive(false); return "OK";
        case "STATE": return new JSONObject().put("known", AudioReceiverService.aliceKnown)
                .put("station", AudioReceiverService.alicePlaying).put("pc", AudioReceiverService.pcPlaying)
                .put("next", AudioReceiverService.pcNextEnabled).put("policy", AudioReceiverService.ambiguityPolicy).toString();
        case "NEXT_PC": return AudioReceiverService.pcPlaying && AudioReceiverService.pcNextEnabled && service.sendControl("MEDIA:NEXT") ? "QUEUED" : "FAILED";
        case "LIKE_PC": return service.bridgeLike();
        case "NEXT_STATION": return AudioReceiverService.alicePlaying && VoiceRouter.sendStationAction(6) ? "QUEUED" : "FAILED";
        case "LIKE_STATION": return AudioReceiverService.alicePlaying && VoiceRouter.sendStationRating(false) ? "QUEUED" : "FAILED";
        default: return "FAILED";
        }
    }
}
