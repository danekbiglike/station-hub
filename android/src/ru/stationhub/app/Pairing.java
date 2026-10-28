package ru.stationhub.app;

import android.content.Context;
import android.content.SharedPreferences;
import java.security.SecureRandom;

public final class Pairing {
    private static final String PREFS = "station_hub";
    private static final String KEY = "pairing_key";
    private Pairing() {}

    public static synchronized String getKey(Context context) {
        SharedPreferences p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String key = p.getString(KEY, null);
        if (key == null || key.length() != 16) {
            byte[] random = new byte[8];
            new SecureRandom().nextBytes(random);
            StringBuilder s = new StringBuilder(16);
            for (byte b : random) s.append(String.format(java.util.Locale.US, "%02x", b & 0xff));
            key = s.toString();
            if (!p.edit().putString(KEY, key).commit()) throw new IllegalStateException("Cannot save pairing key");
        }
        return key;
    }
}
