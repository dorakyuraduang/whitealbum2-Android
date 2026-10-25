package org.wa2.port;

import android.content.Context;

/** Launcher preferences are adapter settings, not separate game logic. */
final class LaunchSettings {
    private LaunchSettings() { }
    static boolean showFps(Context context) {
        return context.getSharedPreferences("launcher", Context.MODE_PRIVATE).getBoolean("show-fps", true);
    }
    static void setShowFps(Context context, boolean value) {
        context.getSharedPreferences("launcher", Context.MODE_PRIVATE).edit().putBoolean("show-fps", value).apply();
    }
    static boolean longPressFastForward(Context context) {
        return context.getSharedPreferences("launcher", Context.MODE_PRIVATE).getBoolean("long-press-fast-forward", true);
    }
    static void setLongPressFastForward(Context context, boolean value) {
        context.getSharedPreferences("launcher", Context.MODE_PRIVATE).edit().putBoolean("long-press-fast-forward", value).apply();
    }
    static boolean testMode(Context context) {
        return context.getSharedPreferences("launcher", Context.MODE_PRIVATE).getBoolean("test-mode", false);
    }
    static void setTestMode(Context context, boolean value) {
        context.getSharedPreferences("launcher", Context.MODE_PRIVATE).edit().putBoolean("test-mode", value).apply();
    }
}
