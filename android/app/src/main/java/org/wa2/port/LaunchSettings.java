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
}
