package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

public final class LegacySettingsPathTest {
    public static void main(String[] args) throws Exception {
        int cases = 0, guards = 0;
        for (String name : new String[] { "SYSTEM.ini", "D3D.ini",
                "Documents/Leaf/WHITE ALBUM2/SYSTEM.ini", "Documents/Leaf/WHITE ALBUM2/D3D.ini" }) {
            String[] parts = LegacySettingsPath.parts(name);
            if (!String.join("/", parts).equals(name) || (parts.length != 1 && parts.length != 4))
                throw new AssertionError("Original settings path changed");
            ++cases;
        }
        for (String name : new String[] { null, "", "../SYSTEM.ini", "/SYSTEM.ini", "SYSTEM.INI",
                "IC/SYSTEM.ini", "Sys.sav", "grp.pak", "Documents/Leaf/OTHER/SYSTEM.ini",
                "Documents/Leaf/WHITE ALBUM2/../SYSTEM.ini", "SYSTEM.ini\u0000.pak" }) {
            boolean caught = false;
            try { LegacySettingsPath.parts(name); } catch (IOException expected) { caught = true; }
            if (!caught) throw new AssertionError("Unexpected settings path accepted: " + name);
            ++guards;
        }
        Files.writeString(Path.of(args[0]), "{\"schema\":\"wa2.android-legacy-settings-path.v1\",\"cases\":" + cases
            + ",\"guards\":" + guards + ",\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n");
        System.out.println("PASS exact original settings paths: " + cases + ", guards: " + guards);
    }
}
