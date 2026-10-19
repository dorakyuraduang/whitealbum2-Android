package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.provider.DocumentsContract;
import java.io.IOException;
import java.util.Locale;

/** Original PAKs stay in the selected local Wa2Res folder; no resource imports. */
final class ResourceDirectory {
    private static final int REQUEST_DIRECTORY = 0x5741;
    private static final Object LOCK = new Object();
    private static final String PREFERENCES = "resource-directory";
    private static final String TREE_KEY = "tree-uri";
    private static Context context;
    private static Activity owner;
    private static final class ArchiveDocument {
        final Uri uri;
        final long size, modifiedMillis;
        ArchiveDocument(Uri uri, long size, long modifiedMillis) {
            this.uri = uri; this.size = size; this.modifiedMillis = modifiedMillis;
        }
    }
    private static ArchiveNames<ArchiveDocument> archives;
    private static String failure = "Resource directory is not initialized";
    private static String gameFailure;
    private static int generation;
    private static Uri preparedTree;
    private static ArchiveNames<ArchiveDocument> preparedArchives;

    private ResourceDirectory() { }

    static void attach(Activity activity) {
        final int token;
        synchronized (LOCK) {
            owner = activity;
            context = activity.getApplicationContext();
            archives = null;
            failure = null;
            gameFailure = null;
            token = ++generation;
        }
        String saved = preferences().getString(TREE_KEY, null);
        Uri tree = saved == null ? null : Uri.parse(saved);
        synchronized (LOCK) {
            if (tree != null && tree.equals(preparedTree) && preparedArchives != null) {
                archives = preparedArchives; preparedArchives = null; preparedTree = null;
                LOCK.notifyAll(); return;
            }
        }
        if (fixedLocation(tree)) load(activity, token, tree, false);
        else {
            // Ignore remembered trees outside the local Wa2Res contract.
            preferences().edit().remove(TREE_KEY).apply();
            if (tree != null) {
                try { context.getContentResolver().releasePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (RuntimeException ignored) { }
            }
            choose(activity, token, null);
        }
    }

    static void detach(Activity activity) {
        synchronized (LOCK) {
            if (owner != activity) return;
            owner = null;
            archives = null;
            failure = "Resource directory was closed";
            gameFailure = null;
            ++generation;
            LOCK.notifyAll();
        }
    }

    private static SharedPreferences preferences() {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }

    static Uri selectedTree(Context application) {
        String saved = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).getString(TREE_KEY, null);
        if (saved == null) return null;
        try { Uri tree = Uri.parse(saved); return fixedLocation(tree) ? tree : null; }
        catch (RuntimeException error) { return null; }
    }

    static String displayLocation(Uri tree) {
        try { return DocumentsContract.getTreeDocumentId(tree).replace("primary:", "内部存储 / ").replace(":", " / "); }
        catch (RuntimeException error) { return "已选择的 Wa2Res"; }
    }

    // Runs only on a launcher worker. Hand the bounded directory index to the
    // SDL activity, avoiding a second provider scan at game initialization.
    static int prepareSelection(Context application, Uri tree, int grantFlags) throws IOException {
        if (Looper.myLooper() == Looper.getMainLooper()) throw new IOException("目录检查必须在后台执行");
        if (!fixedLocation(tree)) throw new IOException("请选择本机存储中的 Wa2Res 文件夹，不能选择其父目录或 IC 子目录。");
        ContentResolver resolver = application.getContentResolver();
        ArchiveNames<ArchiveDocument> found = scan(resolver, tree);
        SharedPreferences prefs = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        String previous = prefs.getString(TREE_KEY, null);
        if ((grantFlags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0)
            resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        // Persist only a verified tree with a durable read grant. Providers
        // without this capability cannot promise a usable next launch.
        boolean durable = false;
        for (android.content.UriPermission permission : resolver.getPersistedUriPermissions())
            if (permission.isReadPermission() && tree.equals(permission.getUri())) durable = true;
        if (!durable) throw new IOException("所选目录未提供持久读取授权，请重新授权。");
        if (!prefs.edit().putString(TREE_KEY, tree.toString()).commit()) throw new IOException("无法保存资源目录设置");
        synchronized (LOCK) { preparedTree = tree; preparedArchives = found; }
        if (previous != null && !previous.equals(tree.toString())) {
            try { resolver.releasePersistableUriPermission(Uri.parse(previous), Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (RuntimeException ignored) { }
        }
        return found.size();
    }

    private static boolean current(Activity activity, int token) {
        synchronized (LOCK) { return owner == activity && generation == token; }
    }

    private static boolean fixedLocation(Uri tree) {
        if (tree == null || !"content".equals(tree.getScheme()) ||
                !DocumentsContract.isTreeUri(tree)) return false;
        try {
            return ResourceLocation.matches(tree.getAuthority(), DocumentsContract.getTreeDocumentId(tree));
        } catch (IllegalArgumentException error) { return false; }
    }

    private static void choose(Activity activity, int token, String problem) {
        if (!current(activity, token) || activity.isFinishing()) return;
        String message = "请选择存放原版资源的 Wa2Res 文件夹（默认：内部共享存储根目录 / Wa2Res）。"
            + "请把原版 .pak 放入 Wa2Res，并保留 IC 子目录。"
            + "首次启动请在系统界面授权读取 Wa2Res；以后会沿用该授权。";
        if (problem != null) message = problem + "\n\n" + message;
        new AlertDialog.Builder(activity).setTitle("游戏资源").setMessage(message)
            .setPositiveButton("授权 Wa2Res", (dialog, which) -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                    Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                    DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID));
                try { activity.startActivityForResult(intent, REQUEST_DIRECTORY); }
                catch (RuntimeException error) { stop(activity, "无法打开系统文件夹选择器"); }
            })
            .setNegativeButton("退出", (dialog, which) -> stop(activity, "未授权读取 Wa2Res"))
            .setOnCancelListener(dialog -> stop(activity, "未授权读取 Wa2Res"))
            .show();
    }

    private static void stop(Activity activity, String problem) {
        synchronized (LOCK) {
            if (owner != activity) return;
            failure = problem;
            LOCK.notifyAll();
        }
        activity.finish();
    }

    static boolean result(Activity activity, int request, int result, Intent data) {
        if (request != REQUEST_DIRECTORY) return false;
        final int token;
        synchronized (LOCK) {
            if (owner != activity) return true;
            token = generation;
        }
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) {
            stop(activity, "未授权读取 Wa2Res");
        } else if (!fixedLocation(data.getData())) {
            choose(activity, token, "目录不符：请选择本机存储中的 Wa2Res，不能选择其父目录或 IC 子目录。");
        } else {
            boolean persist = (data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0 &&
                (data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0;
            load(activity, token, data.getData(), persist);
        }
        return true;
    }

    private static String scanFolder(ContentResolver resolver, Uri tree, String parent,
            String prefix, ArchiveNames<ArchiveDocument> found) throws IOException {
        String introductory = null;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree,
            parent);
        String[] fields = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE, DocumentsContract.Document.COLUMN_LAST_MODIFIED };
        try (Cursor cursor = resolver.query(children, fields, null, null, null)) {
            if (cursor == null) throw new IOException("无法读取资源目录");
            int rows = 0;
            while (cursor.moveToNext()) {
                if (++rows > 10000) throw new IOException("Wa2Res 中的目录条目过多");
                String id = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (id == null || name == null) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (prefix.isEmpty() && name.equalsIgnoreCase("IC")) {
                        if (introductory != null) throw new IOException("序章 IC 资源目录重名");
                        introductory = id;
                    }
                    continue;
                }
                if (!name.toLowerCase(Locale.ROOT).endsWith(".pak")) continue;
                Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                long size = cursor.isNull(3) ? -1 : cursor.getLong(3);
                long modified = cursor.isNull(4) ? 0 : cursor.getLong(4);
                if (size < 0) {
                    try (ParcelFileDescriptor descriptor = resolver.openFileDescriptor(uri, "r")) {
                        if (descriptor != null) size = descriptor.getStatSize();
                    }
                    if (size < 0) throw new IOException("无法取得资源包长度：" + name);
                }
                found.add(prefix + name, new ArchiveDocument(uri, size, Math.max(0, modified)));
            }
        }
        return introductory;
    }

    private static ArchiveNames<ArchiveDocument> scan(ContentResolver resolver, Uri tree) throws IOException {
        ArchiveNames<ArchiveDocument> found = new ArchiveNames<>();
        String introductory = scanFolder(resolver, tree, DocumentsContract.getTreeDocumentId(tree), "", found);
        // The game has a separate introductory chapter. Index only its known
        // child directory; other folders and their contents are never scanned.
        if (introductory != null) scanFolder(resolver, tree, introductory, "IC/", found);
        if (found.size() == 0) throw new IOException("Wa2Res 及其 IC 子目录中没有 .pak 资源包");
        return found;
    }

    private static void load(Activity activity, int token, Uri tree, boolean persist) {
        final Context application = activity.getApplicationContext();
        Thread worker = new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            try {
                if (!fixedLocation(tree)) throw new IOException("资源目录必须是本机存储中的 Wa2Res 文件夹");
                ContentResolver resolver = application.getContentResolver();
                ArchiveNames<ArchiveDocument> found = scan(resolver, tree);
                if (!current(activity, token)) return;
                // Provider IPC and preferences I/O stay outside LOCK. Closing
                // the activity must always be able to wake the SDL thread.
                SharedPreferences prefs = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
                String previous = prefs.getString(TREE_KEY, null);
                if (persist) {
                    try {
                        resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        prefs.edit().putString(TREE_KEY, tree.toString()).apply();
                        if (previous != null && !previous.equals(tree.toString())) {
                            try { resolver.releasePersistableUriPermission(Uri.parse(previous),
                                Intent.FLAG_GRANT_READ_URI_PERMISSION); } catch (SecurityException ignored) { }
                        }
                    } catch (SecurityException ignored) {
                        // The current read grant is still usable for this run.
                        prefs.edit().remove(TREE_KEY).apply();
                    }
                } else if (previous != null && !previous.equals(tree.toString())) {
                    prefs.edit().remove(TREE_KEY).apply();
                }
                synchronized (LOCK) {
                    if (owner != activity || generation != token) return;
                    archives = found;
                    failure = null;
                    LOCK.notifyAll();
                }
            } catch (IOException | RuntimeException error) {
                activity.runOnUiThread(() -> {
                    if (!current(activity, token)) return;
                    preferences().edit().remove(TREE_KEY).apply();
                    String message = error.getMessage();
                    choose(activity, token, message == null ? "无法访问资源目录" : message);
                });
            }
        }, "WA2 resource directory");
        worker.start();
    }

    // Called once when native code opens an archive, from the SDL game thread.
    // Subsequent archive reads use the owned fd directly, with no Java calls.
    public static int openPak(String name) throws IOException {
        return open(name, false);
    }

    public static int openOptionalPak(String name) throws IOException {
        return open(name, true);
    }

    private static void awaitArchives() throws IOException {
        if (Looper.myLooper() == Looper.getMainLooper())
            throw new IOException("Resource files must be accessed off the UI thread");
        while (archives == null && failure == null) {
            try { LOCK.wait(); }
            catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Resource directory selection interrupted", error);
            }
        }
        if (failure != null) throw new IOException(failure);
    }

    // Metadata was gathered by the one directory scan. These calls do not
    // query the provider or open/copy an archive payload for each image load.
    public static String[] listPaks() throws IOException {
        synchronized (LOCK) { awaitArchives(); return archives.names(); }
    }

    public static long[] pakMetadata(String name) throws IOException {
        synchronized (LOCK) {
            awaitArchives();
            ArchiveDocument document = archives.get(name);
            return new long[] { document.size, document.modifiedMillis };
        }
    }

    public static void reportFailure(String message) {
        synchronized (LOCK) {
            if (owner == null || failure != null) return;
            gameFailure = message;
        }
    }

    static boolean finishGame(Activity activity, Runnable finish) {
        final String message;
        synchronized (LOCK) {
            if (owner != activity || gameFailure == null) return false;
            message = gameFailure;
            gameFailure = null;
        }
        // SDLMain calls finish immediately after the native entry returns.
        // Launch the readable failure activity before actually finishing SDL;
        // a queued dialog alone loses a race with Activity.isFinishing().
        activity.runOnUiThread(() -> {
            if (!activity.isFinishing()) {
                Intent intent = new Intent(activity, FailureActivity.class);
                intent.putExtra("message", message);
                activity.startActivity(intent);
            }
            finish.run();
        });
        return true;
    }

    private static int open(String name, boolean optional) throws IOException {
        if (Looper.myLooper() == Looper.getMainLooper())
            throw new IOException("Resource files must be opened off the UI thread");
        final Context application;
        final Uri file;
        synchronized (LOCK) {
            awaitArchives();
            application = context;
            ArchiveDocument document = optional ? archives.find(name) : archives.get(name);
            file = document == null ? null : document.uri;
        }
        if (file == null) return -1;
        try (ParcelFileDescriptor descriptor = application.getContentResolver().openFileDescriptor(file, "r")) {
            if (descriptor == null) throw new IOException("Cannot open resource archive: " + name);
            return descriptor.detachFd();
        }
    }
}
