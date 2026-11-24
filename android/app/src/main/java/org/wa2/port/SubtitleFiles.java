package org.wa2.port;

import java.io.FileNotFoundException;
import java.io.IOException;

import static org.wa2.port.LauncherText.Key.SC_CAPTION_FILE_REQUIRED;

final class SubtitleFiles {
    static final String ENGLISH_ASSET = "todokanai/subtitles";

    private SubtitleFiles() { }

    interface Opener<T> { T open() throws IOException; }
    interface Metadata { String[] get(String name) throws IOException; }

    static byte[] readMain(LaunchProfile profile, Opener<byte[]> selected, Opener<byte[]> english)
            throws IOException {
        if (!profile.enabled(LaunchProfile.CHINESE_SUBTITLES)) return null;
        if (!profile.subtitle.isEmpty()) return selected.open();
        return profile.language == LaunchProfile.ENGLISH ? english.open() : null;
    }

    // Document providers may report an unavailable file as null or an exception.
    static <T> T require(String message, Opener<T> opener) throws IOException {
        try {
            T file = opener.open();
            if (file != null) return file;
        } catch (FileNotFoundException | SecurityException error) {
            throw new IOException(message, error);
        }
        throw new IOException(message);
    }

    static void checkSpecial(LauncherText ui, LaunchProfile profile, Metadata metadata) throws IOException {
        String data = LaunchTarget.SPECIAL.subtitleFile(profile);
        String[] required = profile.language == LaunchProfile.CHINESE
            ? new String[]{data, "subfont.tga", "songfont.tga"} : new String[]{data};
        for (String name : required) {
            String message = ui.text(SC_CAPTION_FILE_REQUIRED, name);
            String[] info = require(message, () -> metadata.get(name));
            if (info.length != 4 || "16".equals(info[3])) throw new IOException(message);
            long size = Long.parseLong(info[1]);
            long limit = name.endsWith(".dat") ? 1024 * 1024 : 8 * 1024 * 1024;
            if (size <= 0 || size > limit) throw new IOException(message);
        }
    }
}
