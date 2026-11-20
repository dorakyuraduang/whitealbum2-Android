package org.wa2.launcher;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.util.function.Predicate;

/** Resource selections and persisted grants for launcher entries. */
public final class GameFolderSelection {
    public static final String SPECIAL = "special";
    public static final String START_SPECIAL = "org.wa2.launcher.action.RUN_SPECIAL_CONTENTS";
    public static final int READ_WRITE = Intent.FLAG_GRANT_READ_URI_PERMISSION
        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
    public static final int TREE_ACCESS = READ_WRITE | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;
    private static final int PERSISTED_ACCESS = READ_WRITE | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION;

    public enum Result { SAVED, ACCESS_REQUIRED, SAVE_FAILED }

    private final SharedPreferences preferences;
    private final ContentResolver resolver;

    public GameFolderSelection(Context context) {
        preferences = context.getSharedPreferences("game-folders", Context.MODE_PRIVATE);
        resolver = context.getContentResolver();
    }

    public Uri saved(String entry) {
        String value = preferences.getString(entry, null);
        return value == null ? null : Uri.parse(value);
    }

    public Intent picker(String entry) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .addFlags(TREE_ACCESS | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        Uri initial = saved(entry);
        if (isTree(initial)) intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, initial);
        return intent;
    }

    private static boolean isTree(Uri uri) {
        return uri != null && ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())
            && uri.getAuthority() != null && uri.getPathSegments().size() == 2
            && DocumentsContract.isTreeUri(uri) && !DocumentsContract.getTreeDocumentId(uri).isEmpty();
    }

    public Uri granted(Uri folder) {
        if (!isTree(folder)) return null;
        for (UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (folder.equals(permission.getUri()) && permission.isReadPermission()
                && permission.isWritePermission()) return folder;
        }
        return null;
    }

    public Result remember(String entry, Intent data, Predicate<Uri> usedByGame) {
        Uri tree = data.getData();
        if (!isTree(tree) || (data.getFlags() & PERSISTED_ACCESS) != PERSISTED_ACCESS)
            return Result.ACCESS_REQUIRED;
        // The primary game launcher uses the same lock when replacing its grant.
        synchronized (GameFolderSelection.class) {
            Uri previous = saved(entry);
            try {
                resolver.takePersistableUriPermission(tree, READ_WRITE);
            } catch (SecurityException error) {
                return Result.ACCESS_REQUIRED;
            }
            if (preferences.edit().putString(entry, tree.toString()).commit()) {
                releaseIfUnused(previous, usedByGame);
                return Result.SAVED;
            }
            // SharedPreferences updates its in-memory view before writing to disk.
            SharedPreferences.Editor rollback = preferences.edit();
            if (previous == null) rollback.remove(entry);
            else rollback.putString(entry, previous.toString());
            rollback.apply();
            releaseIfUnused(tree, usedByGame);
            return Result.SAVE_FAILED;
        }
    }

    public void releaseIfUnused(Uri tree, Predicate<Uri> usedByGame) {
        synchronized (GameFolderSelection.class) {
            if (tree == null || preferences.getAll().containsValue(tree.toString()) || usedByGame.test(tree))
                return;
            try {
                resolver.releasePersistableUriPermission(tree, READ_WRITE);
            } catch (SecurityException | IllegalArgumentException ignored) {
                // The provider may already have revoked this permission.
            }
        }
    }
}
