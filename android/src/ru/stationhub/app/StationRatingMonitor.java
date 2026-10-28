package ru.stationhub.app;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/** Polls the local privileged adapter without blocking audio, UI, or voice routing. */
final class StationRatingMonitor {
    private final AudioReceiverService service;
    private final ArrayBlockingQueue<String[]> actions = new ArrayBlockingQueue<>(2);
    private volatile boolean running;
    private Thread worker;
    StationRatingMonitor(AudioReceiverService service) { this.service=service; }
    void start() { running=true; worker=new Thread(this::loop,"station-hub-rating"); worker.start(); }
    void stop() { running=false; if(worker!=null) worker.interrupt(); }
    boolean remove(String action,String id) {
        if (!("REMOVE_LIKE".equals(action) || "REMOVE_DISLIKE".equals(action)) || !id.matches("[0-9]{1,18}")) return false;
        AudioReceiverService.aliceRatingStatus="PENDING";
        if (!actions.offer(new String[]{action,id})) { AudioReceiverService.aliceRatingStatus=""; return false; }
        return true;
    }
    boolean refresh(String id) {
        if (!id.matches("[0-9]{1,18}")) return false;
        AudioReceiverService.aliceRatingStatus="PENDING";
        if (!actions.offer(new String[]{"REFRESH",id})) { AudioReceiverService.aliceRatingStatus=""; return false; }
        return true;
    }
    private String call(String action,String id) {
        try (Socket s=new Socket()) {
            s.connect(new InetSocketAddress("127.0.0.1",27364),1000); s.setSoTimeout(18000);
            String line=Pairing.getKey(service)+" "+action+" "+id+"\n";
            s.getOutputStream().write(line.getBytes(StandardCharsets.US_ASCII)); s.getOutputStream().flush();
            BufferedReader in=new BufferedReader(new InputStreamReader(s.getInputStream(),StandardCharsets.US_ASCII));
            String result=in.readLine(); return result==null?"UNAVAILABLE":result;
        } catch (Exception ignored) { return "UNAVAILABLE"; }
    }
    private void loop() {
        while (running) {
            try {
                String[] action=actions.poll(2,TimeUnit.SECONDS);
                if (action!=null) {
                    if ("REFRESH".equals(action[0])) Thread.sleep(1500);
                    String result=call(action[0],action[1]);
                    if (action[1].equals(AudioReceiverService.aliceTrackId)) {
                        boolean ok="OK".equals(result)||"LIKE".equals(result)||"DISLIKE".equals(result)||"NONE".equals(result);
                        AudioReceiverService.aliceRatingStatus=ok?"UPDATED":"ERROR";
                    }
                }
                String id=AudioReceiverService.aliceTrackId;
                if (!AudioReceiverService.aliceKnown || !AudioReceiverService.aliceHasMedia || !id.matches("[0-9]{1,18}")) {
                    AudioReceiverService.aliceRatingTrackId=""; AudioReceiverService.aliceRating="UNKNOWN"; continue;
                }
                String rating=call("STATE",id);
                if (id.equals(AudioReceiverService.aliceTrackId)) {
                    AudioReceiverService.aliceRating=("LIKE".equals(rating)||"DISLIKE".equals(rating)||"NONE".equals(rating))?rating:"UNKNOWN";
                    AudioReceiverService.aliceRatingTrackId=id;
                    if (action==null && !"UNKNOWN".equals(AudioReceiverService.aliceRating)) AudioReceiverService.aliceRatingStatus="";
                }
            } catch (InterruptedException ignored) { if (!running) return; }
        }
    }
}
