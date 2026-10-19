package org.wa2.port;

import java.io.IOException;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** Root/IC name index; contains handles, never resource bytes. */
final class ArchiveNames<T> {
    private final Map<String, T> files = new HashMap<>();

    private static String key(String name) throws IOException {
        if (name == null || name.length() < 5 || name.length() > 128)
            throw new IOException("Invalid resource archive name");
        String prefix = "", filename = name;
        if (name.indexOf('/') >= 0) {
            if (!name.regionMatches(true, 0, "IC/", 0, 3))
                throw new IOException("Unknown resource chapter directory");
            prefix = "ic/";
            filename = name.substring(3);
        }
        if (filename.length() < 5) throw new IOException("Expected a PAK filename");
        for (int i = 0; i < filename.length(); ++i) {
            char c = filename.charAt(i);
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
                throw new IOException("Resource archive name must be a filename");
        }
        String folded = filename.toLowerCase(Locale.ROOT);
        if (folded.startsWith(".") || !folded.endsWith(".pak"))
            throw new IOException("Expected a PAK filename");
        return prefix + folded;
    }

    void add(String name, T handle) throws IOException {
        String folded = key(name);
        if (handle == null) throw new IOException("Missing resource handle");
        if (files.putIfAbsent(folded, handle) != null)
            throw new IOException("Ambiguous resource archive name: " + name);
    }

    T get(String name) throws IOException {
        T handle = files.get(key(name));
        if (handle == null) throw new IOException("Resource archive is missing: " + name);
        return handle;
    }

    T find(String name) throws IOException { return files.get(key(name)); }

    int size() { return files.size(); }

    String[] names() {
        String[] names = files.keySet().toArray(new String[0]);
        java.util.Arrays.sort(names);
        return names;
    }
}
