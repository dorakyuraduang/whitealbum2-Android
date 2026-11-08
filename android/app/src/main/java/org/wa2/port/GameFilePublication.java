package org.wa2.port;

import java.io.IOException;
import java.nio.ByteBuffer;

/** Recoverable per-file publication for document providers with rename support.
 * All names are adapter-owned; callers serialize operations in one directory.
 * Payload writes finish and close before the previous published file is moved.
 */
final class GameFilePublication {
    static final String STAGE = ".wa2-new", BACKUP = ".wa2-old";
    interface Backend<T> {
        T find(String name) throws IOException;
        T create(String name) throws IOException;
        T rename(T file, String name) throws IOException;
        void remove(T file) throws IOException;
        void write(T file, ByteBuffer bytes) throws IOException;
    }
    static <T> T recover(Backend<T> files, String name) throws IOException {
        T target = files.find(name), backup = files.find(name + BACKUP), stage = files.find(name + STAGE);
        if (target == null && backup != null) { target = files.rename(backup, name); backup = null; }
        if (stage != null) files.remove(stage);
        if (backup != null) files.remove(backup);
        return target;
    }
    static <T> void replace(Backend<T> files, String name, ByteBuffer bytes) throws IOException {
        T target = recover(files, name), stage = null, backup = null;
        try {
            stage = files.create(name + STAGE);
            files.write(stage, bytes.asReadOnlyBuffer());
            if (target != null) backup = files.rename(target, name + BACKUP);
            files.rename(stage, name); stage = null;
        } catch (IOException | RuntimeException failure) {
            // Keep a recoverable old file even if the provider also fails the
            // rollback. A later open recovers it before exposing the target.
            if (backup != null) try { files.rename(backup, name); } catch (IOException | RuntimeException rollback) { failure.addSuppressed(rollback); }
            if (stage != null) try { files.remove(stage); } catch (IOException | RuntimeException cleanup) { failure.addSuppressed(cleanup); }
            throw failure;
        }
        // Publication is committed. Cleanup failure must not falsely report
        // a failed settings transaction after the new data is already visible.
        if (backup != null) try { files.remove(backup); } catch (IOException | RuntimeException ignored) { }
    }
    private GameFilePublication() { }
}
