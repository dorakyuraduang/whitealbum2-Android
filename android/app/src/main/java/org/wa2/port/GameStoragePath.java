package org.wa2.port;

import java.io.IOException;

/** Selected-directory file allowlist, matching the common C++ storage policy. */
final class GameStoragePath {
    static boolean slot(String name) {
        return name != null && name.length() == 15 && name.startsWith("slot-") && name.endsWith(".wa2sav") &&
            name.charAt(5) >= '0' && name.charAt(5) <= '9' && name.charAt(6) >= '0' && name.charAt(6) <= '9' && name.charAt(7) >= '0' && name.charAt(7) <= '9';
    }
    static boolean valid(String path, boolean write) {
        if (path == null) return false;
        if (path.startsWith("native-test/")) { String child=path.substring(12);return !child.startsWith("native-test/") && valid(child,true); }
        if (path.equals("Sys.sav") || path.equals("native-settings.wa2cfg") || path.equals("native-launcher.wa2cfg")) return true;
        if (path.startsWith("native-saves/") && slot(path.substring(13))) return true;
        if (!write) for (String prefix : LegacySavePath.DIRECTORIES) if (path.equals(prefix + "Sys.sav")) return true;
        return false;
    }
    static String[] parts(String path, boolean write) throws IOException {
        if (!valid(path, write)) throw new IOException("Invalid selected-directory storage path");
        return path.split("/");
    }
    private GameStoragePath() { }
}
