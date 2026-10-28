package ru.stationhub.app;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.text.TextUtils;
import android.transition.ChangeBounds;
import android.transition.Fade;
import android.transition.TransitionManager;
import android.transition.TransitionSet;
import android.view.Gravity;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.URL;
import java.util.Collections;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private final Handler handler = new Handler();
    private final ExecutorService images = Executors.newSingleThreadExecutor();
    private FrameLayout cards;
    private LinearLayout aliceCard, pcCard;
    private TextView aliceTitle, aliceArtist, aliceTime, pcTitle, pcArtist, pcTime, connection, empty;
    private TextView alicePlaceholder, pcPlaceholder;
    private ImageView aliceImage, pcImage;
    private ProgressBar aliceProgress, pcProgress;
    private boolean streamView, initialized, showingAlice, showingPc;
    private String requestedArtwork = "";
    private int imageGeneration;
    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            refresh(true);
            if (streamView && "waiting".equals(intent.getStringExtra("state")) && !AudioReceiverService.alicePlaying) finish();
        }
    };
    private final Runnable tick = new Runnable() {
        @Override public void run() { updateTimes(); handler.postDelayed(this, 1000); }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        streamView = getIntent().getBooleanExtra("stream_view", false);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        setContentView(buildScreen());
        registerReceiver(receiver, new IntentFilter(AudioReceiverService.ACTION_STATE));
        cards.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            if (r - l != or - ol || b - t != ob - ot) arrange(false);
        });
        refresh(false); handler.post(tick); AudioReceiverService.start(this);
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent);
        streamView = intent.getBooleanExtra("stream_view", false); refresh(false);
    }
    @Override protected void onDestroy() {
        unregisterReceiver(receiver); handler.removeCallbacksAndMessages(null); images.shutdownNow();
        super.onDestroy();
    }

    private View buildScreen() {
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff101017); root.setPadding(dp(44), dp(26), dp(44), dp(24));
        LinearLayout header = new LinearLayout(this); header.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(text("STATION  /  HUB", 15, 0xffa9b4cf, true), new LinearLayout.LayoutParams(0, dp(38), 1));
        connection = text("●  Ожидаем компьютер", 14, 0xff8793a9, false);
        connection.setGravity(Gravity.CENTER_VERTICAL | Gravity.RIGHT); header.addView(connection);
        root.addView(header, new LinearLayout.LayoutParams(-1, dp(38)));
        TextView heading = text("Сейчас играет", 34, 0xfff4f6ff, true);
        LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(-1, -2); hp.setMargins(0, dp(14), 0, dp(12)); root.addView(heading, hp);
        cards = new FrameLayout(this); root.addView(cards, new LinearLayout.LayoutParams(-1, 0, 1));
        aliceCard = makeCard(true); pcCard = makeCard(false);
        cards.addView(aliceCard); cards.addView(pcCard);
        empty = text("Здесь появится то, что сейчас играет", 21, 0xff8290a8, false);
        empty.setGravity(Gravity.CENTER); cards.addView(empty, new FrameLayout.LayoutParams(-1, -1));
        TextView foot = text("Адрес: " + localAddress() + "   ·   Ключ: " + Pairing.getKey(this), 12, 0xff78859d, false);
        foot.setGravity(Gravity.CENTER_VERTICAL); root.addView(foot, new LinearLayout.LayoutParams(-1, dp(38)));
        return root;
    }
    private LinearLayout makeCard(boolean alice) {
        int accent = alice ? 0xffaa98ff : 0xff82ddbc;
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(26), dp(23), dp(26), dp(24)); card.setBackground(shape(0xff1e1d29, dp(28)));
        TextView label = text(alice ? "●  СТАНЦИЯ" : "●  КОМПЬЮТЕР", 14, accent, true); card.addView(label);
        LinearLayout content = new LinearLayout(this); content.setGravity(Gravity.CENTER_VERTICAL);
        card.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
        FrameLayout art = new FrameLayout(this); art.setBackground(shape(alice ? 0xff383056 : 0xff24423f, dp(20))); art.setClipToOutline(true);
        ImageView image = new ImageView(this); image.setScaleType(ImageView.ScaleType.CENTER_CROP);
        art.addView(image, new FrameLayout.LayoutParams(-1, -1));
        TextView placeholder = text("♫", 64, alice ? 0xffb7a8e7 : 0xff90d5c5, false); placeholder.setGravity(Gravity.CENTER);
        art.addView(placeholder, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(dp(280), dp(280)); ip.setMargins(0, 0, dp(27), 0); content.addView(art, ip);
        LinearLayout labels = new LinearLayout(this); labels.setOrientation(LinearLayout.VERTICAL); labels.setGravity(Gravity.CENTER_VERTICAL);
        content.addView(labels, new LinearLayout.LayoutParams(0, -1, 1));
        TextView title = text(alice ? "Музыка Станции" : "Звук компьютера", 30, 0xfff6f7ff, true);
        title.setMaxLines(2); title.setEllipsize(TextUtils.TruncateAt.END); labels.addView(title);
        TextView artist = text("", 19, 0xffa3afc3, false); artist.setMaxLines(2); artist.setEllipsize(TextUtils.TruncateAt.END);
        LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(-1, -2); ap.setMargins(0, dp(12), 0, 0); labels.addView(artist, ap);
        TextView playing = text("●  ИГРАЕТ", 13, accent, true);
        LinearLayout.LayoutParams playingParams = new LinearLayout.LayoutParams(-1, -2); playingParams.setMargins(0, dp(28), 0, 0); labels.addView(playing, playingParams);
        ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); bar.setMax(10000);
        bar.setProgressTintList(ColorStateList.valueOf(accent)); bar.setProgressBackgroundTintList(ColorStateList.valueOf(0xff3c4559));
        card.addView(bar, new LinearLayout.LayoutParams(-1, dp(6)));
        TextView time = text("—:—  /  —:—", 14, 0xff98a6bb, false);
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(-1, -2); tp.setMargins(0, dp(10), 0, 0); card.addView(time, tp);
        if (alice) { aliceTitle=title; aliceArtist=artist; aliceTime=time; aliceImage=image; aliceProgress=bar; alicePlaceholder=placeholder; }
        else { pcTitle=title; pcArtist=artist; pcTime=time; pcImage=image; pcProgress=bar; pcPlaceholder=placeholder; }
        return card;
    }
    private void refresh(boolean animate) {
        boolean newAlice = AudioReceiverService.aliceKnown && AudioReceiverService.alicePlaying;
        boolean newPc = AudioReceiverService.pcPlaying || "playing".equals(AudioReceiverService.currentState);
        aliceTitle.setText(AudioReceiverService.aliceTitle.isEmpty() ? "Музыка Станции" : AudioReceiverService.aliceTitle);
        aliceArtist.setText(AudioReceiverService.aliceArtist.isEmpty() ? "Алиса" : AudioReceiverService.aliceArtist);
        pcTitle.setText(AudioReceiverService.currentTitle.isEmpty() ? "Звук компьютера" : AudioReceiverService.currentTitle);
        pcArtist.setText(AudioReceiverService.currentArtist.isEmpty() ? "Компьютер" : AudioReceiverService.currentArtist);
        String state = AudioReceiverService.currentState;
        boolean connected = "connected".equals(state) || "playing".equals(state);
        connection.setText(connected ? "●  Компьютер подключён" : "●  Компьютер не подключён");
        connection.setTextColor(connected ? 0xff82ddbc : 0xff8793a9);
        if (AudioReceiverService.pcArtwork != null) { pcImage.setImageBitmap(AudioReceiverService.pcArtwork); pcPlaceholder.setVisibility(View.GONE); }
        else { pcImage.setImageDrawable(null); pcPlaceholder.setVisibility(View.VISIBLE); }
        loadAliceArtwork(AudioReceiverService.aliceArtwork);
        if (!initialized || showingAlice != newAlice || showingPc != newPc) {
            showingAlice = newAlice; showingPc = newPc; arrange(animate && initialized); initialized = true;
        }
        updateTimes();
    }
    private void arrange(boolean animate) {
        if (cards == null || cards.getWidth() == 0) return;
        if (animate) {
            TransitionSet changes = new TransitionSet(); changes.setOrdering(TransitionSet.ORDERING_TOGETHER);
            ChangeBounds move = new ChangeBounds();
            move.addTarget(aliceCard); move.addTarget(pcCard);
            Fade fade = new Fade();
            fade.addTarget(aliceCard); fade.addTarget(pcCard);
            changes.addTransition(move); changes.addTransition(fade);
            changes.setDuration(420); changes.setInterpolator(new DecelerateInterpolator());
            TransitionManager.beginDelayedTransition(cards, changes);
        }
        int width = cards.getWidth(), height = Math.min(cards.getHeight(), dp(520)), gap = dp(22);
        boolean both = showingAlice && showingPc;
        int group = Math.min(width, dp(both ? 1500 : 740));
        int cardWidth = both ? (group - gap) / 2 : group;
        int left = (width - group) / 2;
        setCardPosition(aliceCard, cardWidth, height, both ? left : (width - cardWidth) / 2);
        setCardPosition(pcCard, cardWidth, height, both ? left + cardWidth + gap : (width - cardWidth) / 2);
        aliceCard.setVisibility(showingAlice ? View.VISIBLE : View.GONE);
        pcCard.setVisibility(showingPc ? View.VISIBLE : View.GONE);
        empty.setVisibility(!showingAlice && !showingPc ? View.VISIBLE : View.GONE);
    }
    private void setCardPosition(View card, int width, int height, int left) {
        FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(Math.max(1,width), Math.max(1,height), Gravity.LEFT | Gravity.CENTER_VERTICAL);
        p.leftMargin = Math.max(0,left); card.setLayoutParams(p);
    }
    private void updateTimes() {
        long now = android.os.SystemClock.elapsedRealtime();
        updateTime(aliceTime, aliceProgress, AudioReceiverService.alicePositionMs, AudioReceiverService.aliceDurationMs,
                AudioReceiverService.alicePositionAt, AudioReceiverService.alicePlaying, now);
        updateTime(pcTime, pcProgress, AudioReceiverService.pcPositionMs, AudioReceiverService.pcDurationMs,
                AudioReceiverService.pcPositionAt, AudioReceiverService.pcPlaying, now);
    }
    private void updateTime(TextView label, ProgressBar bar, long position, long duration, long at, boolean playing, long now) {
        if (position >= 0 && playing && at > 0) position += Math.min(5000, Math.max(0, now - at));
        if (duration > 0 && position > duration) position = duration;
        label.setText(clock(position) + "  /  " + clock(duration));
        bar.setProgress(duration > 0 && position >= 0 ? (int)Math.min(10000, position * 10000 / duration) : 0);
        bar.setAlpha(duration > 0 ? 1f : 0.35f);
    }
    private String clock(long millis) {
        if (millis < 0) return "—:—";
        long secs = millis / 1000;
        if (secs >= 3600) return String.format(Locale.ROOT, "%d:%02d:%02d", secs/3600, (secs/60)%60, secs%60);
        return String.format(Locale.ROOT, "%d:%02d", secs/60, secs%60);
    }
    private void loadAliceArtwork(String source) {
        if (source == null) source = "";
        if (source.equals(requestedArtwork)) return;
        requestedArtwork = source; int generation = ++imageGeneration; aliceImage.setImageDrawable(null); alicePlaceholder.setVisibility(View.VISIBLE);
        String value = source.replace("%%", "400x400");
        if (!value.startsWith("https://")) value = "https://" + value;
        Uri uri = Uri.parse(value); String host = uri.getHost();
        if (host == null || !(host.equals("avatars.yandex.net") || host.equals("avatars.mds.yandex.net"))) return;
        final String url = value;
        images.execute(() -> {
            Bitmap bitmap = null; HttpURLConnection conn = null;
            try {
                conn = (HttpURLConnection)new URL(url).openConnection(); conn.setConnectTimeout(2500); conn.setReadTimeout(2500);
                conn.setInstanceFollowRedirects(false);
                if (conn.getResponseCode() == 200) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream(); byte[] buf = new byte[8192];
                    try (InputStream in = conn.getInputStream()) {
                        int n; while ((n=in.read(buf)) > 0 && out.size() + n <= 1024 * 1024) out.write(buf, 0, n);
                    }
                    byte[] bytes = out.toByteArray(); BitmapFactory.Options o = new BitmapFactory.Options(); o.inJustDecodeBounds=true;
                    BitmapFactory.decodeByteArray(bytes,0,bytes.length,o);
                    if (o.outWidth > 0 && o.outHeight > 0) {
                        o.inJustDecodeBounds=false; o.inSampleSize=1;
                        while (o.outWidth / o.inSampleSize > 512 || o.outHeight / o.inSampleSize > 512) o.inSampleSize *= 2;
                        bitmap=BitmapFactory.decodeByteArray(bytes,0,bytes.length,o);
                    }
                }
            } catch (Exception ignored) {} finally { if (conn != null) conn.disconnect(); }
            final Bitmap result=bitmap;
            handler.post(() -> { if (!isFinishing() && generation == imageGeneration) {
                aliceImage.setImageBitmap(result); alicePlaceholder.setVisibility(result == null ? View.VISIBLE : View.GONE);
            }});
        });
    }
    private static GradientDrawable shape(int color, int radius) { GradientDrawable d=new GradientDrawable(); d.setColor(color); d.setCornerRadius(radius); return d; }
    private TextView text(String value, int size, int color, boolean bold) {
        TextView v=new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(color);
        if (bold) v.setTypeface(Typeface.DEFAULT,Typeface.BOLD); return v;
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private String localAddress() {
        try { for (NetworkInterface n: Collections.list(NetworkInterface.getNetworkInterfaces())) {
            if (!n.isUp() || n.isLoopback()) continue;
            for (InetAddress a: Collections.list(n.getInetAddresses())) if (a instanceof Inet4Address && a.isSiteLocalAddress()) return a.getHostAddress();
        }} catch (Exception ignored) {}
        return "нет сети";
    }
}
