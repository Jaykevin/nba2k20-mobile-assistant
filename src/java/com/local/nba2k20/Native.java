package com.local.nba2k20;

import android.util.Log;

/** JNI bridge to lib2k20float.so. Every call is safe when the library failed to load. */
final class Native {
    static final String TAG = "2K20Assist";
    private static boolean loaded;
    private static int status = -99;

    private Native() { }

    private static native int init();
    private static native void set(int id, int value);
    private static native long stat(int id);
    private static native int hookCount();
    private static native int career(int op, int arg);

    static final int CAREER_GET_HEIGHT = 0, CAREER_SET_HEIGHT = 1;

    /** Career player operations; negative = unavailable (-1 no career save loaded). */
    static int careerOp(int op, int arg) {
        if (!loaded) return -2;
        try { return career(op, arg); } catch (Throwable error) { return -2; }
    }

    /** Loads the library and installs hooks. Returns hook count (> 0) or a negative error. */
    static synchronized int start() {
        if (status > 0) return status;
        try {
            if (!loaded) {
                System.loadLibrary("2k20float");
                loaded = true;
            }
            status = init();
        } catch (Throwable error) {
            Log.e(TAG, "native module unavailable", error);
            status = -98;
        }
        Log.i(TAG, "native start status=" + status);
        return status;
    }

    static boolean ready() { return status > 0; }
    static int status() { return status; }

    static int expectedHooks() {
        try { return loaded ? hookCount() : 0; } catch (Throwable error) { return 0; }
    }

    static void put(int id, int value) {
        if (!loaded) return;
        try { set(id, value); } catch (Throwable error) { Log.e(TAG, "set failed", error); }
    }

    static long get(int id) {
        if (!loaded) return 0;
        try { return stat(id); } catch (Throwable error) { return 0; }
    }
}
