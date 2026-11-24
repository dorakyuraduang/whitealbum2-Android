package org.wa2.port;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.wa2.port.LauncherText.Key.EXTERNAL_CAPTION_FILE_UNAVAILABLE;
import static org.wa2.port.LauncherText.Key.SC_CAPTION_FILE_REQUIRED;

public final class SubtitleFilesTest {
    private interface Attempt { void run() throws IOException; }

    private static void check(boolean value) { if (!value) throw new AssertionError(); }

    private static void failsWith(String message, Throwable cause, Attempt attempt) throws IOException {
        try {
            attempt.run();
        } catch (IOException error) {
            check(message.equals(error.getMessage()) && error.getCause() == cause);
            return;
        }
        throw new AssertionError("Expected a subtitle file error");
    }

    public static void main(String[] args) throws Exception {
        String[][] files = {{"subtext.dat", "subfont.tga", "songfont.tga"}, {"subtext_ja.dat"}, {"subtext_en.dat"}};
        String[] regularFile = {"file", "100", "0", "128"};
        Set<String> specialMessages = new HashSet<>(), externalMessages = new HashSet<>();
        int missingFiles = 0, unavailableDocuments = 0;
        for (int i = 0; i < LauncherText.count(); i++) {
            LauncherText ui = LauncherText.at(i);
            check(specialMessages.add(ui.text(SC_CAPTION_FILE_REQUIRED, "subtext_ja.dat")));
            String external = ui.text(EXTERNAL_CAPTION_FILE_UNAVAILABLE);
            check(externalMessages.add(external));
            for (int language = 0; language < files.length; language++) {
                LaunchProfile profile = new LaunchProfile(LaunchProfile.CHINESE_SUBTITLES, language, "");
                List<String> requested = new ArrayList<>();
                SubtitleFiles.checkSpecial(ui, profile, name -> { requested.add(name); return regularFile; });
                check(requested.equals(Arrays.asList(files[language])));
                for (String missing : files[language]) {
                    String message = ui.text(SC_CAPTION_FILE_REQUIRED, missing);
                    check(message.contains(missing));
                    failsWith(message, null, () -> SubtitleFiles.checkSpecial(ui, profile,
                        name -> name.equals(missing) ? new String[0] : regularFile));
                    missingFiles++;
                }
            }
            FileNotFoundException removed = new FileNotFoundException("provider detail");
            SecurityException revoked = new SecurityException("provider detail");
            failsWith(external, null, () -> SubtitleFiles.require(external, () -> null));
            failsWith(external, removed, () -> SubtitleFiles.require(external, () -> { throw removed; }));
            failsWith(external, revoked, () -> SubtitleFiles.require(external, () -> { throw revoked; }));
            unavailableDocuments += 3;
        }

        LauncherText english = LauncherText.resolve("en", "ja-JP");
        LaunchProfile japanese = new LaunchProfile(LaunchProfile.CHINESE_SUBTITLES, LaunchProfile.JAPANESE, "");
        String expected = "SC captions need subtext_ja.dat. Add the file for this game language to the selected folder or turn off extra captions.";
        FileNotFoundException removed = new FileNotFoundException();
        failsWith(expected, removed, () -> SubtitleFiles.checkSpecial(english, japanese, name -> { throw removed; }));
        for (String[] invalid : new String[][]{{"file","0","0","128"}, {"file","1048577","0","128"}, {"folder","1","0","16"}})
            failsWith(expected, null, () -> SubtitleFiles.checkSpecial(english, japanese, name -> invalid));
        // Other I/O failures retain the provider's diagnostic.
        IOException readError = new IOException("read failed");
        failsWith(readError.getMessage(), null, () -> SubtitleFiles.require(expected, () -> { throw readError; }));

        check(SubtitleFiles.ENGLISH_ASSET.equals("todokanai/subtitles"));
        byte[] builtInEnglish = {1};
        byte[] externalFile = {2};
        String selected = "content://provider/tree/patch/document/todokanai%2Fsubtitles";
        int mainSourceCases = 0;
        for (int language = LaunchProfile.CHINESE; language <= LaunchProfile.UKRAINIAN; language++) {
            for (int flags : new int[]{0, LaunchProfile.CHINESE_SUBTITLES}) {
                for (String uri : new String[]{"", selected}) {
                    LaunchProfile profile = LaunchProfile.decode(new LaunchProfile(flags, language, uri).encode());
                    boolean useExternal = flags != 0 && uri.equals(selected);
                    boolean useEnglish = flags != 0 && uri.isEmpty() && language == LaunchProfile.ENGLISH;
                    byte[] data = SubtitleFiles.readMain(profile, () -> {
                        check(useExternal && profile.subtitle.equals(selected));
                        return externalFile;
                    }, () -> {
                        check(useEnglish);
                        return builtInEnglish;
                    });
                    check(data == (useExternal ? externalFile : useEnglish ? builtInEnglish : null));
                    mainSourceCases++;
                }
            }
        }
        LaunchProfile englishProfile = japanese.withLanguage(LaunchProfile.ENGLISH);
        check(SubtitleFiles.readMain(englishProfile, () -> { throw new AssertionError(); }, () -> null) == null);
        failsWith(readError.getMessage(), null, () -> SubtitleFiles.readMain(englishProfile.withSubtitle(selected),
            () -> { throw readError; }, () -> { throw new AssertionError(); }));

        String report = "{\"passed\":true,\"sc_missing_file_cases\":" + missingFiles
            + ",\"external_unavailable_cases\":" + unavailableDocuments
            + ",\"main_caption_source_cases\":" + mainSourceCases + "}\n";
        Files.write(Paths.get(args[0]), report.getBytes(StandardCharsets.UTF_8));
    }
}
