package ru.stationhub.app;

import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/** Starts only our fixed local bootstrap through the already-installed root shell. */
final class BridgeBootstrap {
    private BridgeBootstrap() {}
    static void startIfInstalled() {
        new Thread(() -> {
            try (Socket s = new Socket()) {
                s.connect(new InetSocketAddress("127.0.0.1", 2323), 2000);
                s.setSoTimeout(3000);
                String command = "if [ -f /data/local/stationhub-bridge/autorun ] && [ -x /data/local/stationhub-bridge/bootstrap.sh ]; then " +
                        "nohup /system/bin/sh /data/local/stationhub-bridge/bootstrap.sh >/dev/null 2>&1 </dev/null & fi\nexit\n";
                s.getOutputStream().write(command.getBytes(StandardCharsets.US_ASCII)); s.getOutputStream().flush();
                byte[] discard = new byte[1024];
                while (s.getInputStream().read(discard) >= 0) { /* wait for shell exit */ }
            } catch (Exception ignored) {}
        }, "station-hub-bootstrap").start();
    }
}
