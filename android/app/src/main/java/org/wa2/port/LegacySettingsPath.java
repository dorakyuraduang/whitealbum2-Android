package org.wa2.port;

import java.io.IOException;

/** Exact read-only source settings paths; independent of the PAK namespace. */
final class LegacySettingsPath {
    private LegacySettingsPath() { }
    static String[] parts(String name) throws IOException {
        if (!("SYSTEM.ini".equals(name) || "D3D.ini".equals(name) ||
              "Documents/Leaf/WHITE ALBUM2/SYSTEM.ini".equals(name) ||
              "Documents/Leaf/WHITE ALBUM2/D3D.ini".equals(name)))
            throw new IOException("Invalid original settings path");
        return name.split("/");
    }
}
