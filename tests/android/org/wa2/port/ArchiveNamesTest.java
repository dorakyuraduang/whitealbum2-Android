package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

public final class ArchiveNamesTest {
    interface Action { void run() throws IOException; }
    private static int cases;
    private static void check(boolean value) {
        if (!value) throw new AssertionError("Archive index check failed");
        ++cases;
    }
    private static void rejects(Action action) throws IOException {
        try { action.run(); } catch (IOException expected) { ++cases; return; }
        throw new AssertionError("Invalid/ambiguous archive accepted");
    }
    public static void main(String[] args) throws Exception {
        ArchiveNames<String> names = new ArchiveNames<>();
        names.add("BGM.PAK", "music");
        names.add("ck-gal.pak", "script");
        check(names.get("bgm.pak").equals("music"));
        check(names.get("CK-GAL.PAK").equals("script"));
        check(names.size() == 2);
        rejects(() -> names.add("bgm.pak", "duplicate"));
        check(names.get("BGM.PAK").equals("music"));
        rejects(() -> names.get("missing.pak"));
        check(names.find("missing.pak") == null);
        check(names.find("bgm.pak").equals("music"));
        rejects(() -> names.find("../missing.pak"));
        for (String invalid : new String[] { "../BGM.PAK", "a/b.pak", "a\\b.pak", "/bgm.pak",
                "bgm.pak\0", ".pak", "a.exe", "", null, "语音.pak" })
            rejects(() -> names.get(invalid));
        rejects(() -> names.add("a.pak", null));
        names.add("IC/BGM.PAK", "intro-music");
        names.add("ic/mv010.pak", "intro-movie");
        check(names.get("ic/bgm.pak").equals("intro-music"));
        check(names.get("IC/MV010.PAK").equals("intro-movie"));
        check(names.get("BGM.PAK").equals("music"));
        rejects(() -> names.add("ic/BGM.pak", "duplicate"));
        for (String invalid : new String[] { "IC/../BGM.PAK", "IC/a/b.pak", "IC//a.pak", "IC/.pak",
                "IC/", "IC\\a.pak", "/IC/a.pak", "CC/a.pak" })
            rejects(() -> names.get(invalid));
        Locale previous = Locale.getDefault();
        try {
            Locale.setDefault(Locale.forLanguageTag("tr-TR"));
            names.add("VOICE.PAK", "voice");
            check(names.get("voice.pak").equals("voice"));
        } finally { Locale.setDefault(previous); }
        Files.writeString(Path.of(args[0]), "{\"schema\":\"wa2.android-archive-names.v1\",\"cases\":" + cases +
            ",\"android_device_verified\":false,\"passed\":true}\n");
        System.out.println("PASS archive filenames: " + cases);
    }
}
