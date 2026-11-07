package org.wa2.port;

import java.io.IOException;
import java.util.Locale;

/** Read-only file access bounds; directory/bank selection belongs to C++. */
final class LegacySavePath {
    private LegacySavePath() { }
    static final String[] DIRECTORIES = { "", "Documents/Leaf/WHITE ALBUM2/",
        "Documents/Leaf/WHITE ALBUM2 -closing chapter-/",
        "Documents/Leaf/WHITE ALBUM2 -introductory chapter-/" };
    static boolean valid(String path) {
        if (path == null || path.length() > 128) return false;
        String name = path;
        if (path.indexOf('/') >= 0) {
            boolean found = false;
            for (int n = 1; n < DIRECTORIES.length; ++n) if (path.startsWith(DIRECTORIES[n])) {
                name = path.substring(DIRECTORIES[n].length()); found = true; break;
            }
            if (!found) return false;
        }
        name = name.toLowerCase(Locale.ROOT);
        if ("save_q.sav".equals(name)) return true;
        return name.length() == 11 && name.startsWith("save_") && name.endsWith(".sav") &&
            name.charAt(5) >= '0' && name.charAt(5) <= '9' && name.charAt(6) >= '0' && name.charAt(6) <= '9';
    }
    static String[] parts(String path) throws IOException {
        if (!valid(path)) throw new IOException("Invalid original manual-save path");
        return path.split("/");
    }
}
