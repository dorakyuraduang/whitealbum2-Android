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
import java.util.ArrayList;
import java.nio.ByteBuffer;

/** Original PAKs stay in the user-selected folder; no resource imports. */
final class ResourceDirectory {
    private static final int REQUEST_DIRECTORY = 0x5741;
    private static final Object LOCK = new Object();
    private static final Object STORAGE_LOCK = new Object();
    private static final Object LOOSE_LOCK = new Object();
    private static ReadOnlyDocuments looseDocuments;
    private static int looseGeneration=-1;
    private static final int TREE_ACCESS = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
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
    private static Uri activeTree;
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
            activeTree = null;
            failure = null;
            gameFailure = null;
            token = ++generation;
            looseDocuments=null;looseGeneration=-1;
        }
        String saved = preferences().getString(TREE_KEY, null);
        Uri tree = saved == null ? null : Uri.parse(saved);
        synchronized (LOCK) {
            if (tree != null && tree.equals(preparedTree) && preparedArchives != null) {
                archives = preparedArchives; activeTree = tree; preparedArchives = null; preparedTree = null;
                LOCK.notifyAll(); return;
            }
        }
        if (validTree(tree)) load(activity, token, tree, false);
        else {
            // Discard malformed or non-tree selections, independent of name.
            preferences().edit().remove(TREE_KEY).apply();
            if (tree != null) {
                try { context.getContentResolver().releasePersistableUriPermission(tree,
                    TREE_ACCESS); } catch (RuntimeException ignored) { }
            }
            choose(activity, token, null);
        }
    }

    static void detach(Activity activity) {
        synchronized (LOCK) {
            if (owner != activity) return;
            owner = null;
            archives = null;
            activeTree = null;
            failure = "Resource directory was closed";
            gameFailure = null;
            ++generation;
            looseDocuments=null;looseGeneration=-1;
            LOCK.notifyAll();
        }
    }

    private static SharedPreferences preferences() {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }

    static Uri selectedTree(Context application) {
        String saved = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE).getString(TREE_KEY, null);
        if (saved == null) return null;
        try { Uri tree = Uri.parse(saved); return validTree(tree) ? tree : null; }
        catch (RuntimeException error) { return null; }
    }
    static void validatePreparedLanguages(Context application,String script,String font) throws IOException {
        Uri tree=selectedTree(application);
        synchronized(LOCK){
            if(tree==null || !tree.equals(preparedTree) || preparedArchives==null)throw new IOException("请先重新检查游戏资源文件夹。");
            for(String name:new String[]{script,font})if(!name.isEmpty() && preparedArchives.find(name)==null)
                throw new IOException("所选游戏语言缺少资源包："+name+"。请把对应 PAK 放入游戏目录根目录，或选择其他游戏语言。");
        }
    }

    static String displayLocation(Uri tree) {
        try { return DocumentsContract.getTreeDocumentId(tree).replace("primary:", "内部存储 / ").replace(":", " / "); }
        catch (RuntimeException error) { return "已选择的游戏资源目录"; }
    }

    // Runs only on a launcher worker. Hand the bounded directory index to the
    // SDL activity, avoiding a second provider scan at game initialization.
    static int prepareSelection(Context application, Uri tree, int grantFlags) throws IOException {
        if (Looper.myLooper() == Looper.getMainLooper()) throw new IOException("目录检查必须在后台执行");
        if (!validTree(tree)) throw new IOException("请选择存放原版 PAK 的游戏资源文件夹，名称不限。");
        ContentResolver resolver = application.getContentResolver();
        ArchiveNames<ArchiveDocument> found = scan(resolver, tree);
        SharedPreferences prefs = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        String previous = prefs.getString(TREE_KEY, null);
        if ((grantFlags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0)
            resolver.takePersistableUriPermission(tree, grantFlags & TREE_ACCESS);
        // Persist only a verified tree with durable read/write grants. Providers
        // without this capability cannot promise a usable next launch.
        boolean durable = false;
        for (android.content.UriPermission permission : resolver.getPersistedUriPermissions())
            if (permission.isReadPermission() && permission.isWritePermission() && tree.equals(permission.getUri())) durable = true;
        if (!durable) throw new IOException("游戏存档和配置需要所选目录的持久读写授权，请重新选择文件夹授权。");
        if (!prefs.edit().putString(TREE_KEY, tree.toString()).commit()) throw new IOException("无法保存资源目录设置");
        synchronized (LOCK) { preparedTree = tree; preparedArchives = found; }
        if (previous != null && !previous.equals(tree.toString())) {
            try { resolver.releasePersistableUriPermission(Uri.parse(previous), TREE_ACCESS); }
            catch (RuntimeException ignored) { }
        }
        return found.size();
    }

    private static boolean current(Activity activity, int token) {
        synchronized (LOCK) { return owner == activity && generation == token; }
    }

    private static boolean validTree(Uri tree) {
        if (tree == null || !"content".equals(tree.getScheme()) ||
                !DocumentsContract.isTreeUri(tree)) return false;
        try {
            return ResourceLocation.matches(tree.getAuthority(), DocumentsContract.getTreeDocumentId(tree));
        } catch (IllegalArgumentException error) { return false; }
    }

    private static void choose(Activity activity, int token, String problem) {
        if (!current(activity, token) || activity.isFinishing()) return;
        String message = "请选择存放原版 .pak 的游戏资源文件夹，文件夹名称不限，并保留原有 IC 子目录。"
            + "请授权读写所选目录，存档和游戏配置也保存在这里；以后会沿用该授权。";
        if (problem != null) message = problem + "\n\n" + message;
        new AlertDialog.Builder(activity).setTitle("游戏资源").setMessage(message)
            .setPositiveButton("选择并授权文件夹", (dialog, which) -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(TREE_ACCESS |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                    Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                    DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID));
                try { activity.startActivityForResult(intent, REQUEST_DIRECTORY); }
                catch (RuntimeException error) { stop(activity, "无法打开系统文件夹选择器"); }
            })
            .setNegativeButton("退出", (dialog, which) -> stop(activity, "未授权游戏资源目录"))
            .setOnCancelListener(dialog -> stop(activity, "未授权游戏资源目录"))
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
            stop(activity, "未授权游戏资源目录");
        } else if (!validTree(data.getData())) {
            choose(activity, token, "请选择系统文件夹选择器提供的资源目录，文件夹名称不限。");
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
                if (++rows > 10000) throw new IOException("所选资源目录中的条目过多");
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
        if (found.size() == 0) throw new IOException("所选目录及其 IC 子目录中没有 .pak 资源包");
        return found;
    }

    private static void load(Activity activity, int token, Uri tree, boolean persist) {
        final Context application = activity.getApplicationContext();
        Thread worker = new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            try {
                if (!validTree(tree)) throw new IOException("资源目录必须通过系统文件夹选择器授权，名称不限");
                ContentResolver resolver = application.getContentResolver();
                ArchiveNames<ArchiveDocument> found = scan(resolver, tree);
                if (!current(activity, token)) return;
                // Provider IPC and preferences I/O stay outside LOCK. Closing
                // the activity must always be able to wake the SDL thread.
                SharedPreferences prefs = application.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
                String previous = prefs.getString(TREE_KEY, null);
                if (persist) {
                        resolver.takePersistableUriPermission(tree, TREE_ACCESS);
                        prefs.edit().putString(TREE_KEY, tree.toString()).apply();
                        if (previous != null && !previous.equals(tree.toString())) {
                            try { resolver.releasePersistableUriPermission(Uri.parse(previous),
                                TREE_ACCESS); } catch (SecurityException ignored) { }
                        }
                } else if (previous != null && !previous.equals(tree.toString())) {
                    prefs.edit().remove(TREE_KEY).apply();
                }
                boolean durable = false;
                for (android.content.UriPermission permission : resolver.getPersistedUriPermissions())
                    if (permission.isReadPermission() && permission.isWritePermission() && tree.equals(permission.getUri())) durable = true;
                if (!durable) throw new IOException("存档和配置需要所选目录的持久读写授权，请重新选择文件夹。");
                synchronized (LOCK) {
                    if (owner != activity || generation != token) return;
                    archives = found;
                    activeTree = tree;
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

    // Separate from the PAK index: only the two original settings filenames
    // can be read, and native policy decides whether an import is needed.
    public static int openLegacySettings(String name) throws IOException {
        final String[] parts = LegacySettingsPath.parts(name);
        return openLegacyFile(parts);
    }

    public static int openLegacySave(String name) throws IOException {
        return openLegacyFile(LegacySavePath.parts(name));
    }

    public static int gameStorageToken() throws IOException {
        synchronized (LOCK) { awaitArchives(); if (activeTree == null) throw new IOException("Game storage needs an authorized directory"); return generation; }
    }
    private static ReadOnlyDocuments looseDocuments(int token) throws IOException {
        synchronized(LOCK){awaitArchives();if(activeTree==null || token!=generation)throw new IOException("Resource directory changed; restart the game");
            if(looseDocuments==null || looseGeneration!=token || !looseDocuments.belongsTo(activeTree)){looseDocuments=new ReadOnlyDocuments(context.getContentResolver(),activeTree);looseGeneration=token;}return looseDocuments;}
    }
    public static String[] looseResourceMetadata(String name,int token,boolean list) throws IOException {
        ReadOnlyDocuments files=looseDocuments(token);synchronized(LOOSE_LOCK){return files.metadata(name,list);}
    }
    public static int openLooseResource(String name,int token) throws IOException {
        ReadOnlyDocuments files=looseDocuments(token);synchronized(LOOSE_LOCK){return files.open(name);}
    }
    private static GameDocuments gameDocuments(int token) throws IOException {
        synchronized (LOCK) {
            awaitArchives();
            if (activeTree == null || token != generation) throw new IOException("Game directory changed; return to the launcher and restart the game");
            return new GameDocuments(context.getContentResolver(),activeTree);
        }
    }
    public static int openGameFile(String name,int token) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized (STORAGE_LOCK) { return files.open(name); }
    }
    public static void replaceGameFile(String name,int token,ByteBuffer bytes) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized (STORAGE_LOCK) { files.replace(name,bytes); }
    }
    public static void updateGameFile(String name,int token,long expected,long offset,ByteBuffer bytes) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized (STORAGE_LOCK) { files.update(name,expected,offset,bytes); }
    }
    public static String[] gameSaveMetadata(int token,String directory) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized (STORAGE_LOCK) { return files.saves(directory); }
    }
    public static int compatibleOpen(String name,int token,int access,int creation) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleOpen(name,access,creation);}
    }
    public static String[] compatibleMetadata(String name,int token,boolean list) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleMetadata(name,list);}
    }
    public static String[] compatibleReadMetadata(String name,int token,boolean list) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleReadMetadata(name,list);}
    }
    public static int compatibleReadOpen(String name,String reference,int token) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleReadOpen(name,reference);}
    }
    public static int[] compatibleReadBatch(String[] names,String[] references,int token) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleReadBatch(names,references);}
    }
    public static int compatibleOperation(String name,String target,int token,int operation,int flags) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){return files.compatibleOperation(name,target,operation,flags);}
    }
    public static void compatibleReplace(String name,int token,ByteBuffer bytes) throws IOException {
        GameDocuments files=gameDocuments(token);synchronized(STORAGE_LOCK){files.compatibleReplace(name,bytes);}
    }

    private static int openLegacyFile(String[] parts) throws IOException {
        final Context application;
        final Uri tree;
        synchronized (LOCK) { awaitArchives(); application = context; tree = activeTree; }
        if (tree == null) throw new IOException("Original files need an authorized game directory");
        final ContentResolver resolver = application.getContentResolver();
        String parent = DocumentsContract.getTreeDocumentId(tree);
        for (int n = 0; n < parts.length; ++n) {
            parent = settingsChild(resolver, tree, parent, parts[n], n + 1 < parts.length);
            if (parent == null) return -1;
        }
        Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, parent);
        try (ParcelFileDescriptor descriptor = resolver.openFileDescriptor(file, "r")) {
            if (descriptor == null) throw new IOException("Cannot open original read-only file");
            return descriptor.detachFd();
        }
    }

    // One bounded metadata scan, never a hundred opens or any payload copies.
    // Triples are canonical relative path, byte size and modification millis.
    public static String[] legacySaveMetadata() throws IOException {
        final Context application; final Uri tree;
        synchronized (LOCK) { awaitArchives(); application = context; tree = activeTree; }
        if (tree == null) throw new IOException("Original saves need an authorized game directory");
        final ContentResolver resolver = application.getContentResolver();
        final ArrayList<String> result = new ArrayList<>();
        for (String prefix : LegacySavePath.DIRECTORIES) {
            String parent = DocumentsContract.getTreeDocumentId(tree);
            if (!prefix.isEmpty()) for (String component : prefix.split("/")) {
                parent = settingsChild(resolver, tree, parent, component, true);
                if (parent == null) break;
            }
            if (parent == null) continue;
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
            String[] fields = { DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED };
            try (Cursor cursor = resolver.query(children, fields, null, null, null)) {
                if (cursor == null) throw new IOException("Cannot inspect original manual-save directory");
                int rows = 0;
                while (cursor.moveToNext()) {
                    if (++rows > 10000) throw new IOException("Original manual-save directory has too many entries");
                    String name = cursor.getString(0);
                    if (name == null || !LegacySavePath.valid(prefix + name)) continue;
                    long size = DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(1)) || cursor.isNull(2) ? 0 : Math.max(0, cursor.getLong(2));
                    long modified = cursor.isNull(3) ? 0 : Math.max(0, cursor.getLong(3));
                    if (result.size() >= 404 * 3) throw new IOException("Original manual-save inventory exceeds its limit");
                    result.add(prefix + name); result.add(Long.toString(size)); result.add(Long.toString(modified));
                }
            }
        }
        return result.toArray(new String[0]);
    }

    private static String settingsChild(ContentResolver resolver, Uri tree,
            String parent, String expected, boolean directory) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
        String[] fields = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE };
        String found = null;
        try (Cursor cursor = resolver.query(children, fields, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot inspect original settings directory");
            int rows = 0;
            while (cursor.moveToNext()) {
                if (++rows > 10000) throw new IOException("Original settings directory has too many entries");
                String id = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (name == null || !name.equalsIgnoreCase(expected)) continue;
                if (found != null) throw new IOException("Ambiguous original settings path: " + expected);
                if (id == null || DocumentsContract.Document.MIME_TYPE_DIR.equals(mime) != directory)
                    throw new IOException("Original settings path has the wrong file type: " + expected);
                found = id;
            }
        }
        return found;
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
