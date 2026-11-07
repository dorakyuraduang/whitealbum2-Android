package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

public final class LegacySavePathTest {
    public static void main(String[] args) throws Exception {
        int cases = 0, guards = 0;
        for (String directory : LegacySavePath.DIRECTORIES) {
            for (int n = 0; n <= 100; ++n) {
                String name = n == 100 ? "save_Q.sav" : String.format(Locale.ROOT, "save_%02d.sav", n);
                String path = directory + name;
                if (!LegacySavePath.valid(path) || !String.join("/", LegacySavePath.parts(path)).equals(path))
                    throw new AssertionError("Original save path changed");
                ++cases;
            }
        }
        for (String path : new String[] { "SAVE_99.SAV", "SaVe_q.SaV" }) {
            if (!LegacySavePath.valid(path)) throw new AssertionError("Original ASCII case was lost");
            ++cases;
        }
        for (String path : new String[] { null, "", "save_0.sav", "save_000.sav", "save_0x.sav",
            "save_00.sav/", "../save_00.sav", "/save_00.sav", "IC/save_00.sav",
            "Documents/Leaf/OTHER/save_00.sav", "save_00.sav.bak", "save_00.sav\\x", "game.exe",
            "Sys.sav", "grp.pak", "save_00.sav\u0000", "Documents/Leaf/WHITE ALBUM2/../save_00.sav" }) {
            boolean caught = false;
            try { LegacySavePath.parts(path); } catch (IOException expected) { caught = true; }
            if (!caught || LegacySavePath.valid(path)) throw new AssertionError("Unknown save path was accepted");
            ++guards;
        }
        Files.writeString(Path.of(args[0]), "{\"schema\":\"wa2.android-legacy-save-path.v1\",\"cases\":" + cases
            + ",\"guards\":" + guards + ",\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n");
        System.out.println("PASS original read-only manual-save paths: " + cases + ", guards: " + guards);
    }
}
