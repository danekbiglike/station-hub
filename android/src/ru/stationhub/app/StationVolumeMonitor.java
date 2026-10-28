package ru.stationhub.app;

import java.io.DataInputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/** Reads the Station's reported volume without changing it. */
final class StationVolumeMonitor {
    private volatile boolean running;
    private volatile Socket socket;
    void start() { running = true; new Thread(this::run, "station-hub-volume").start(); }
    void stop() { running = false; try { if (socket != null) socket.close(); } catch (Exception ignored) {} }
    private void run() {
        while (running) {
            try (Socket s = new Socket()) {
                socket = s; s.connect(new InetSocketAddress("127.0.0.1", 9901), 1000);
                DataInputStream in = new DataInputStream(s.getInputStream());
                while (running) {
                    int n = Integer.reverseBytes(in.readInt()); if (n <= 0 || n > 600000) break;
                    byte[] data = new byte[n]; in.readFully(data);
                    VoiceRouter.Value kind = VoiceRouter.find(data, 0, n, 253, 0);
                    VoiceRouter.Value body = kind != null && kind.number == 42 ? VoiceRouter.find(data, 0, n, 252, 2) : null;
                    if (body == null) continue;
                    VoiceRouter.Value level = VoiceRouter.find(data, body.offset, body.offset + body.length, 7, 0);
                    if (level != null && level.number >= 0 && level.number <= 10) AudioReceiverService.stationVolume = (int) level.number;
                }
            } catch (Exception ignored) {} finally { socket = null; AudioReceiverService.stationVolume = -1; }
            if (running) try { Thread.sleep(1500); } catch (InterruptedException ignored) {}
        }
    }
}
