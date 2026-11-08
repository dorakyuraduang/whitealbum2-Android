package org.wa2.port;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

/** Launcher preferences are adapter settings, not separate game logic. */
final class LaunchSettings {
    static final String FILE="native-launcher.wa2cfg";
    private static final String SESSION="wa2.launch-profile", SESSION_TREE="wa2.launch-tree";
    private static final Object LOCK=new Object();
    private static final Map<String,LaunchProfileStore> profiles=new HashMap<>();
    private static final ExecutorService IO=Executors.newSingleThreadExecutor(action -> new Thread(() -> {Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);action.run();},"WA2 launcher configuration"));
    private LaunchSettings() { }
    private static LaunchProfile previous(Context context) {
        android.content.SharedPreferences old=context.getSharedPreferences("launcher",Context.MODE_PRIVATE);
        return new LaunchProfile((old.getBoolean("show-fps",true)?LaunchProfile.FPS:0) | (old.getBoolean("long-press-fast-forward",true)?LaunchProfile.LONG_PRESS:0) | (old.getBoolean("test-mode",false)?LaunchProfile.TEST_MODE:0));
    }
    private static String key(Context context) { Uri tree=ResourceDirectory.selectedTree(context);return tree==null?null:tree.toString(); }
    private static LaunchProfileStore store(Context context) { synchronized(LOCK){return profiles.get(key(context));} }
    private static LaunchProfile session(Context context) {
        if(!(context instanceof Wa2Activity))return null;Intent intent=((Activity)context).getIntent();String selected=key(context);
        if(intent==null || selected==null || !selected.equals(intent.getStringExtra(SESSION_TREE)))return null;
        try{return LaunchProfile.decode(intent.getByteArrayExtra(SESSION));}catch(IOException error){return null;}
    }
    static boolean sessionValid(Activity activity) { return session(activity)!=null; }
    static void applySession(Context context,Intent intent) {
        LaunchProfileStore store=store(context);String selected=key(context);if(store==null || selected==null || store.dirty())throw new IllegalStateException("Launcher settings are not saved in the selected game directory");
        intent.putExtra(SESSION,store.current().encode());intent.putExtra(SESSION_TREE,selected);
    }
    private static LaunchProfile profile(Context context) { LaunchProfile session=session(context);if(session!=null)return session;LaunchProfileStore store=store(context);return store==null?previous(context):store.current(); }
    static boolean showFps(Context context) { return profile(context).enabled(LaunchProfile.FPS); }
    static boolean longPressFastForward(Context context) { return profile(context).enabled(LaunchProfile.LONG_PRESS); }
    static boolean testMode(Context context) { return profile(context).enabled(LaunchProfile.TEST_MODE); }
    static boolean saving(Context context) {LaunchProfileStore current=store(context);return current!=null && current.dirty();}
    // Called after read/write authorization on a launcher worker. Queueing on
    // the same executor drains earlier toggle writes before reload/game start.
    static void prepare(Context context,Uri tree,boolean importPrevious) throws IOException {
        if(Looper.myLooper()==Looper.getMainLooper())throw new IOException("Launcher storage must run off the UI thread");
        final Context application=context.getApplicationContext();final String selected=tree.toString();
        try { IO.submit(() -> {
            LaunchProfileStore current;synchronized(LOCK){current=profiles.get(selected);}
            if(current==null) {
                final GameDocuments documents=new GameDocuments(application.getContentResolver(),tree);
                LaunchProfileStore.Storage files=new LaunchProfileStore.Storage() {
                    @Override public byte[] read() throws IOException {
                        int descriptor=documents.open(FILE);if(descriptor<0)return null;
                        try(ParcelFileDescriptor.AutoCloseInputStream input=new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(descriptor))) {
                            byte[] bytes=new byte[16];int at=0;while(at<bytes.length){int count=input.read(bytes,at,bytes.length-at);if(count<0)throw new IOException("Truncated launcher configuration");if(count==0)throw new IOException("Short launcher configuration read");at+=count;}
                            if(input.read()!=-1)throw new IOException("Launcher configuration exceeds its size bound");return bytes;
                        }
                    }
                    @Override public void replace(byte[] bytes) throws IOException { documents.replace(FILE,ByteBuffer.wrap(bytes)); }
                };
                current=new LaunchProfileStore(files,importPrevious?previous(application):LaunchProfile.defaults());
            } else { current.flush();current.reload(); }
            current.flush();synchronized(LOCK){profiles.put(selected,current);}return null;
        }).get(); } catch(InterruptedException error){Thread.currentThread().interrupt();throw new IOException("Launcher configuration interrupted",error);}
        catch(ExecutionException error){Throwable cause=error.getCause();if(cause instanceof IOException)throw (IOException)cause;throw new IOException("Cannot access launcher configuration",cause);}
    }
    private static void set(Context context,int flag,boolean value,Consumer<String> completed) {
        final LaunchProfileStore store=store(context);if(store==null){completed.accept("请先选择并检查游戏文件夹。");return;}
        if(!store.set(flag,value)){completed.accept(null);return;}
        IO.execute(() -> {String problem=null;try{store.flush();}catch(IOException | RuntimeException error){problem=error.getMessage();if(problem==null)problem="无法保存启动配置";}completed.accept(problem);});
    }
    static void setShowFps(Context context,boolean value,Consumer<String> completed) { set(context,LaunchProfile.FPS,value,completed); }
    static void setLongPressFastForward(Context context,boolean value,Consumer<String> completed) { set(context,LaunchProfile.LONG_PRESS,value,completed); }
    static void setTestMode(Context context,boolean value,Consumer<String> completed) { set(context,LaunchProfile.TEST_MODE,value,completed); }
}
