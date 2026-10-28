package ru.stationhub.app;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Process;

/**
 * Headless setup endpoint for the Android shell. It is not a LAN service:
 * the desktop reads it through an already-authorized ADB connection. Android's
 * DUMP permission and this UID check both apply; ordinary apps cannot query it.
 */
public final class PairingProvider extends ContentProvider {
    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri uri, String[] projection, String selection,
                                  String[] selectionArgs, String sortOrder) {
        int caller=Binder.getCallingUid();
        if (caller!=0 && caller!=2000 && caller!=Process.myUid())
            throw new SecurityException("Only the device owner via ADB may read the pairing key");
        if (uri==null || !"/key".equals(uri.getPath())) throw new IllegalArgumentException("Unknown path");
        MatrixCursor result=new MatrixCursor(new String[]{"pairing_key"},1);
        result.addRow(new Object[]{Pairing.getKey(getContext())});
        return result;
    }
    @Override public String getType(Uri uri) { return "vnd.android.cursor.item/vnd.stationhub.pairing"; }
    @Override public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException(); }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { throw new UnsupportedOperationException(); }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) { throw new UnsupportedOperationException(); }
}
