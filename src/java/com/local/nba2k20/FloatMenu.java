package com.local.nba2k20;

import android.app.Activity;
import android.util.Log;

/**
 * Entry points called from the game activity (smali patch): onCreate -> start, onResume -> resume,
 * onPause -> pause, onDestroy -> stop. Signatures must not change.
 */
public final class FloatMenu {
    private static Overlay overlay;
    private static Activity owner;

    private FloatMenu() { }

    public static void start(final Activity activity) {
        activity.runOnUiThread(() -> {
            try {
                if (overlay != null && owner == activity) {
                    overlay.show();
                    return;
                }
                if (overlay != null) overlay.destroy();
                owner = activity;
                overlay = new Overlay(activity);
            } catch (Throwable error) {
                Log.e(Native.TAG, "panel startup failed", error);
            }
        });
    }

    public static void resume(Activity activity) {
        try {
            if (overlay != null && owner == activity) overlay.show();
        } catch (Throwable error) {
            Log.e(Native.TAG, "resume failed", error);
        }
    }

    public static void pause(Activity activity) {
        try {
            if (overlay != null && owner == activity) overlay.hide();
        } catch (Throwable error) {
            Log.e(Native.TAG, "pause failed", error);
        }
    }

    public static void stop(Activity activity) {
        try {
            if (overlay != null && owner == activity) {
                overlay.destroy();
                overlay = null;
                owner = null;
            }
        } catch (Throwable error) {
            Log.e(Native.TAG, "stop failed", error);
        }
    }
}
