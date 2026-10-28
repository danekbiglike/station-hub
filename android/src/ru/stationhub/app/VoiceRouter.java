package ru.stationhub.app;

import android.os.Handler;
import android.os.HandlerThread;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** Handles only explicit mode phrases. Other recognized speech is discarded. */
final class VoiceRouter {
    private static final int PORT = 9875;
    private static final int MAX_FRAME = 600000;
    private final AudioReceiverService service;
    private final HandlerThread handlerThread = new HandlerThread("station-hub-voice-dispatch");
    private Handler handler;
    private volatile boolean running;
    private volatile Socket socket;
    private Thread reader;
    private Runnable pending;
    private long lastCommandAt;
    private String lastCommand = "";
    private final AtomicBoolean speaking = new AtomicBoolean(false);
    private volatile long pendingChoiceUntil;
    private volatile String pendingChoiceAction = "";

    VoiceRouter(AudioReceiverService service) { this.service = service; }

    void start() {
        running = true;
        handlerThread.start();
        handler = new Handler(handlerThread.getLooper());
        reader = new Thread(this::listen, "station-hub-voice");
        reader.start();
    }
    void stop() {
        running = false;
        try { if (socket != null) socket.close(); } catch (Exception ignored) {}
        if (handler != null) handler.removeCallbacksAndMessages(null);
        handlerThread.quitSafely();
    }

    private void listen() {
        while (running) {
            try (Socket s = new Socket()) {
                socket = s;
                s.connect(new InetSocketAddress("127.0.0.1", PORT), 1000);
                DataInputStream in = new DataInputStream(s.getInputStream());
                while (running) {
                    int size = Integer.reverseBytes(in.readInt());
                    if (size <= 0 || size > MAX_FRAME) break;
                    byte[] frame = new byte[size];
                    in.readFully(frame);
                    Speech speech = recognizedText(frame);
                    if (speech != null) onSpeech(speech.text, speech.fastCommand);
                }
            } catch (Exception ignored) {
                // A disconnected voice service is retried without storing speech.
            } finally { socket = null; }
            if (running) try { Thread.sleep(1500); } catch (InterruptedException ignored) {}
        }
    }

    private void onSpeech(String text, boolean fastCommand) {
        String normalized = text.toLowerCase(new Locale("ru", "RU"))
                .replaceAll("[^\\p{L}\\p{N}\\s]", " ").replaceAll("\\s+", " ").trim();
        if (normalized.startsWith("алиса ")) normalized = normalized.substring(6).trim();
        normalized = normalized.replace(" п к", " пк");
        final String command;
        switch (normalized) {
            case "игровой режим": case "включи игровой режим": case "режим игры":
            case "включи режим игры": case "переключи на игровой режим": command = "MODE:GAME"; break;
            case "качественный режим": case "включи качественный режим": case "режим качества":
            case "включи режим качества": case "переключи на качественный режим": command = "MODE:QUALITY"; break;
            case "следующий трек на компьютере": case "следующий трек на пк":
            case "следующую песню на компьютере": case "следующую песню на пк":
            case "дальше на компьютере": case "дальше на пк": command = "MEDIA:NEXT"; break;
            case "лайк на компьютере": case "лайк на пк": case "поставь лайк на компьютере":
            case "поставь лайк на пк": command = "MEDIA:LIKE"; break;
            case "дальше": case "следующий трек": case "следующую песню": command = "ASK:NEXT"; break;
            case "лайк": case "поставь лайк": command = "ASK:LIKE"; break;
            case "компьютер": case "на компьютере": case "пк": case "на пк": command = "CHOICE:PC"; break;
            case "станция": case "на станции": case "алиса": case "на алисе": command = "CHOICE:ALICE"; break;
            default: command = null;
        }
        handler.post(() -> {
            if (pending != null) handler.removeCallbacks(pending);
            pending = null;
            if (command == null) return;
            pending = () -> {
                long now = android.os.SystemClock.elapsedRealtime();
                if (command.equals(lastCommand) && now - lastCommandAt < 3000) return;
                if (command.startsWith("CHOICE:")) {
                    if (pendingChoiceUntil == 0 || now > pendingChoiceUntil) return;
                    String chosenAction = pendingChoiceAction;
                    pendingChoiceUntil = 0;
                    pendingChoiceAction = "";
                    cancelDialog();
                    if (command.equals("CHOICE:PC")) {
                        boolean supported = AudioReceiverService.pcPlaying && (chosenAction.equals("LIKE") || AudioReceiverService.pcNextEnabled);
                        if (!(supported && service.sendControl("MEDIA:" + chosenAction)))
                            speakAsync(chosenAction.equals("LIKE") ? "Не удалось отметить трек на компьютере." : "Компьютерный плеер не поддерживает переключение.");
                    } else {
                        if (chosenAction.equals("LIKE")) sendStationRating(false); else sendStationAction(6);
                    }
                    lastCommand = command; lastCommandAt = now;
                    return;
                }
                if (command.equals("ASK:NEXT") || command.equals("ASK:LIKE")) {
                    // Alice can execute these before the recognition event reaches us.
                    // Leave ambiguous phrases to her until interception is reliable.
                    return;
                }
                if (command.equals("MEDIA:NEXT") && !(AudioReceiverService.pcPlaying && AudioReceiverService.pcNextEnabled)) {
                    cancelDialog(); speakAsync("Компьютерный плеер сейчас не поддерживает переключение."); return;
                }
                if (command.equals("MEDIA:LIKE") && !AudioReceiverService.pcPlaying) {
                    cancelDialog(); speakAsync("На компьютере сейчас ничего не играет."); return;
                }
                if (service.sendControl(command)) {
                    lastCommand = command;
                    lastCommandAt = now;
                    cancelDialog();
                    if (command.equals("MODE:GAME")) speakAsync("Игровой режим включён");
                    else if (command.equals("MODE:QUALITY")) speakAsync("Качественный режим включён");
                }
            };
            handler.postDelayed(pending, 500);
        });
    }

    private static void cancelDialog() {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1", PORT), 500);
            ByteArrayOutputStream method = new ByteArrayOutputStream();
            varint(method, 8); varint(method, 3);
            ByteArrayOutputStream remoting = new ByteArrayOutputStream();
            bytes(remoting, 7, "AliceCapability".getBytes(StandardCharsets.UTF_8));
            bytes(remoting, 8, method.toByteArray());
            ByteArrayOutputStream msg = new ByteArrayOutputStream();
            bytes(msg, 252, remoting.toByteArray());
            varint(msg, 253L << 3); varint(msg, 210);
            byte[] payload = msg.toByteArray();
            OutputStream out = s.getOutputStream();
            int n = payload.length;
            out.write(new byte[]{(byte)n, (byte)(n >> 8), (byte)(n >> 16), (byte)(n >> 24)});
            out.write(payload); out.flush();
        } catch (Exception ignored) {}
    }
    private static void varint(ByteArrayOutputStream out, long n) {
        while (n >= 128) { out.write((int)(n & 127) | 128); n >>>= 7; }
        out.write((int)n);
    }
    private static void bytes(ByteArrayOutputStream out, int id, byte[] b) {
        varint(out, ((long)id << 3) | 2); varint(out, b.length); out.write(b, 0, b.length);
    }
    private void speakAsync(String text) {
        speakAsync(text, false);
    }
    void onControlResult(String code) {
        switch (code) {
            case "LIKE:OK": speakAsync("Поставила лайк на компьютере"); break;
            case "LIKE:ALREADY": speakAsync("Этот трек уже отмечен на компьютере"); break;
            case "LIKE:NO_PLAYER": speakAsync("Открой вкладку Яндекс Музыки с играющим треком"); break;
            case "LIKE:FAILED": speakAsync("Не удалось поставить лайк на компьютере"); break;
        }
    }
    private void speakAsync(String text, boolean listenAfter) {
        if (!speaking.compareAndSet(false, true)) return;
        new Thread(() -> {
            try {
                Thread.sleep(250); // Let the previous dialog cancellation reach Alice first.
                try (Socket s = new Socket()) {
                    s.connect(new InetSocketAddress("127.0.0.1", PORT), 500); s.setSoTimeout(1000);
                    ByteArrayOutputStream subscribe = new ByteArrayOutputStream();
                    bytes(subscribe, 1, "AliceCapability".getBytes(StandardCharsets.UTF_8));
                    ByteArrayOutputStream directive = new ByteArrayOutputStream();
                    bytes(directive, 1, text.getBytes(StandardCharsets.UTF_8));
                    varint(directive, 2L << 3); varint(directive, 1);
                    ByteArrayOutputStream method = new ByteArrayOutputStream();
                    varint(method, 1L << 3); varint(method, 18);
                    varint(method, 3L << 3); varint(method, 1);
                    bytes(method, 13, directive.toByteArray());
                    OutputStream out = s.getOutputStream();
                    writeRpc(out, 20, subscribe.toByteArray());
                    writeRpc(out, 8, method.toByteArray());
                    out.flush();
                    DataInputStream in = new DataInputStream(s.getInputStream());
                    long deadline = android.os.SystemClock.elapsedRealtime() + 8000;
                    boolean started = false;
                    while (running && android.os.SystemClock.elapsedRealtime() < deadline) {
                        try {
                            int n = Integer.reverseBytes(in.readInt());
                            if (n <= 0 || n > MAX_FRAME) break;
                            byte[] frame = new byte[n]; in.readFully(frame);
                            Value kind = find(frame, 0, n, 253, 0);
                            Value body = kind != null && (kind.number == 3 || kind.number == 210)
                                    ? find(frame, 0, n, 252, 2) : null;
                            if (body == null) continue;
                            Value event = find(frame, body.offset, body.offset + body.length, 18, 2);
                            if (event == null) continue;
                            Value code = find(frame, event.offset, event.offset + event.length, 1, 0);
                            if (code != null && code.number == 6) started = true;
                            else if (code != null && (code.number == 4 || (code.number == 1 && started))) break;
                        } catch (java.net.SocketTimeoutException ignored) {}
                    }
                }
            } catch (Exception ignored) {
                // Lack of spoken feedback does not change the requested mode.
            } finally { speaking.set(false); }
            if (listenAfter && running && pendingChoiceUntil > android.os.SystemClock.elapsedRealtime()) startFollowup();
        }, "station-hub-voice-feedback").start();
    }
    private void startFollowup() {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1", PORT), 500); s.setSoTimeout(1000);
            ByteArrayOutputStream method = new ByteArrayOutputStream();
            varint(method, 1L << 3); varint(method, 2); // START_CONVERSATION
            varint(method, 3L << 3); varint(method, 1);
            OutputStream out = s.getOutputStream(); writeRpc(out, 8, method.toByteArray()); out.flush();
            InputStream in = s.getInputStream(); byte[] ignored = new byte[2048];
            while (running && pendingChoiceUntil > android.os.SystemClock.elapsedRealtime()) {
                try { if (in.read(ignored) < 0) break; }
                catch (java.net.SocketTimeoutException timeout) {}
            }
        } catch (Exception ignored) {}
    }
    static boolean sendStationAction(int action) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1", PORT), 500);
            ByteArrayOutputStream method = new ByteArrayOutputStream();
            varint(method, 1L << 3); varint(method, action); // PlaybackControlCapability NEXT / LIKE
            OutputStream out = s.getOutputStream();
            writeRpc(out, "PlaybackControlCapability", 13, method.toByteArray()); out.flush();
            Thread.sleep(250);
            return true;
        } catch (Exception ignored) { return false; }
    }
    static boolean sendStationRating(boolean dislike) {
        return sendStationText(dislike ? "мне не нравится этот трек" : "мне нравится этот трек");
    }
    static boolean sendStationVolume(int level) {
        if (level < 1 || level > 10) return false;
        return sendStationText("громкость на " + level + " из 10");
    }
    private static boolean sendStationText(String text) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1", PORT), 500);
            org.json.JSONObject event = new org.json.JSONObject().put("type", "text_input")
                    .put("text", text);
            ByteArrayOutputStream request = new ByteArrayOutputStream();
            bytes(request, 2, event.toString().getBytes(StandardCharsets.UTF_8));
            varint(request, 18L << 3); varint(request, 1); // TEXT_INPUT
            ByteArrayOutputStream method = new ByteArrayOutputStream();
            varint(method, 1L << 3); varint(method, 0); // START_REQUEST
            bytes(method, 2, request.toByteArray());
            OutputStream out=s.getOutputStream(); writeRpc(out, 8, method.toByteArray()); out.flush();
            Thread.sleep(250); return true;
        } catch (Exception ignored) { return false; }
    }
    private static void writeRpc(OutputStream out, int field, byte[] payload) throws java.io.IOException {
        writeRpc(out, "AliceCapability", field, payload);
    }
    private static void writeRpc(OutputStream out, String target, int field, byte[] payload) throws java.io.IOException {
        ByteArrayOutputStream remote = new ByteArrayOutputStream();
        bytes(remote, 7, target.getBytes(StandardCharsets.UTF_8));
        bytes(remote, field, payload);
        ByteArrayOutputStream envelope = new ByteArrayOutputStream();
        bytes(envelope, 252, remote.toByteArray()); varint(envelope, 253L << 3); varint(envelope, 210);
        byte[] msg = envelope.toByteArray(); int n = msg.length;
        out.write(new byte[]{(byte)n, (byte)(n >> 8), (byte)(n >> 16), (byte)(n >> 24)}); out.write(msg);
    }

    private static final class Speech {
        final String text; final boolean fastCommand;
        Speech(String text, boolean fastCommand) { this.text = text; this.fastCommand = fastCommand; }
    }
    private static Speech recognizedText(byte[] frame) {
        try {
            Value kind = find(frame, 0, frame.length, 253, 0);
            Value state = kind != null && kind.number == 3 ? find(frame, 0, frame.length, 252, 2)
                    : find(frame, 0, frame.length, 3, 2);
            if (state == null) return null;
            int end = state.offset + state.length;
            Value source = find(frame, state.offset, end, 10, 2);
            boolean fastCommand = false;
            if (source != null) {
                Value sourceName = find(frame, source.offset, source.offset + source.length, 6, 2);
                if (sourceName != null && "command".equals(sourceName.text(frame))) {
                    fastCommand = true;
                    Value request = find(frame, state.offset, end, 15, 2);
                    if (request != null) {
                        int requestEnd = request.offset + request.length;
                        Value finalFlag = find(frame, request.offset, requestEnd, 2, 0);
                        Value transcript = find(frame, request.offset, requestEnd, 1, 2);
                        if (finalFlag != null && finalFlag.number != 0 && transcript != null) return new Speech(transcript.text(frame), true);
                    }
                }
            }
            Value transcript = find(frame, state.offset, end, 2, 2);
            return transcript == null ? null : new Speech(transcript.text(frame), fastCommand);
        } catch (Exception ignored) { return null; }
    }
    static final class Value {
        long number; int offset; int length;
        String text(byte[] data) { return new String(data, offset, length, StandardCharsets.UTF_8); }
    }
    private static long readVarint(byte[] data, int end, int[] pos) {
        long n = 0;
        for (int shift = 0; shift < 64 && pos[0] < end; shift += 7) {
            int b = data[pos[0]++] & 255;
            n |= (long)(b & 127) << shift;
            if ((b & 128) == 0) return n;
        }
        throw new IllegalArgumentException("Invalid protobuf");
    }
    static Value find(byte[] data, int start, int end, int field, int wireWanted) {
        int[] pos = {start};
        while (pos[0] < end) {
            long tag = readVarint(data, end, pos);
            int wire = (int)(tag & 7);
            Value v = new Value();
            if (wire == 0) v.number = readVarint(data, end, pos);
            else if (wire == 2) {
                long length = readVarint(data, end, pos);
                if (length < 0 || length > end - pos[0]) throw new IllegalArgumentException("Invalid length");
                v.offset = pos[0]; v.length = (int)length; pos[0] += v.length;
            } else if (wire == 1 && end - pos[0] >= 8) pos[0] += 8;
            else if (wire == 5 && end - pos[0] >= 4) pos[0] += 4;
            else throw new IllegalArgumentException("Invalid wire type");
            if ((tag >>> 3) == field && wire == wireWanted) return v;
        }
        return null;
    }
}
