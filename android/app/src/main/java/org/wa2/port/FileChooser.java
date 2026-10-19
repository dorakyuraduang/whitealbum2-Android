package org.wa2.port;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.FileObserver;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** System document selection for the common legacy file-dialog interface. */
public final class FileChooser {
    private static final int DOCUMENT_REQUEST=0x5742;
    private static final long MAX_DOCUMENT_BYTES=64L*1024*1024;
    private static Activity activity;
    private static int active;
    private static JSONObject request;
    private static AlertDialog folderDialog;
    private static final ArrayList<FileObserver> exports=new ArrayList<>();
    private static final ExecutorService io=Executors.newSingleThreadExecutor(work -> {
        Thread thread=new Thread(work,"WA2 document I/O");thread.setPriority(Thread.MIN_PRIORITY);return thread;
    });
    private static native void nativeSelection(int token,String path);
    public static void attach(Activity owner) { activity=owner; }
    public static void detach() {
        if(folderDialog!=null)folderDialog.dismiss();folderDialog=null;
        if(active!=0)nativeSelection(active,null);active=0;request=null;activity=null;
        // Export observers remain alive until their final source writer closes.
        // Their callbacks retain only the application context and selected URI.
    }
    private static void finish(String path) { final int token=active;active=0;request=null;if(token!=0)nativeSelection(token,path); }
    private static void fail(Throwable error) {
        final Activity owner=activity;if(owner!=null)owner.runOnUiThread(() -> new AlertDialog.Builder(owner)
            .setTitle("文件操作失败").setMessage(error.getMessage()).setPositiveButton(android.R.string.ok,(d,w) -> finish(null)).setOnCancelListener(d -> finish(null)).show());
        else finish(null);
    }
    public static void choose(int token,String json) {
        final Activity owner=activity;if(owner==null) { nativeSelection(token,null);return; }
        owner.runOnUiThread(() -> { try {
            if(active!=0)throw new IllegalStateException("File chooser already active");active=token;request=new JSONObject(json);
            if(request.getBoolean("directory")) { browseFolder(new File(request.getString("root")),new File(request.getString("initial")));return; }
            Intent intent=new Intent(request.getBoolean("save")?Intent.ACTION_CREATE_DOCUMENT:Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("application/octet-stream");
            if(request.getBoolean("save")) { String name=request.getString("name");if(name.isEmpty())name="wa2-save";String extension=request.getString("extension");if(!extension.isEmpty() && name.lastIndexOf('.')<0)name+="."+extension;intent.putExtra(Intent.EXTRA_TITLE,name); }
            owner.startActivityForResult(intent,DOCUMENT_REQUEST);
        } catch(Throwable error) { fail(error); } });
    }
    private static void browseFolder(File root,File requested) throws Exception {
        root=root.getCanonicalFile();File current=requested.getCanonicalFile();if(!current.toPath().startsWith(root.toPath()) || !current.isDirectory())current=root;
        final File base=root,directory=current;File[] found=directory.listFiles(File::isDirectory);if(found==null)found=new File[0];if(found.length>4096)throw new IllegalStateException("Folder entry limit");
        Arrays.sort(found,Comparator.comparing(File::getName,String.CASE_INSENSITIVE_ORDER));ArrayList<File> choices=new ArrayList<>();ArrayList<String> labels=new ArrayList<>();
        if(!directory.equals(base)) { choices.add(directory.getParentFile());labels.add("..（上一级）"); }
        for(File child:found)if(!child.getName().startsWith(".")) { choices.add(child);labels.add(child.getName()); }
        ListView list=new ListView(activity);list.setAdapter(new ArrayAdapter<>(activity,android.R.layout.simple_list_item_1,labels));
        LinearLayout layout=new LinearLayout(activity);layout.setOrientation(LinearLayout.VERTICAL);TextView caption=new TextView(activity);
        caption.setText(directory.equals(base)?"Wa2Res（当前游戏目录）":base.toPath().relativize(directory.toPath()).toString());caption.setPadding(16,8,16,8);layout.addView(caption);layout.addView(list,new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,300));
        folderDialog=new AlertDialog.Builder(activity).setTitle(request.getString("title")).setView(layout)
            .setPositiveButton("选择此目录",(dialog,which) -> finish(directory.getPath())).setNegativeButton(android.R.string.cancel,(dialog,which) -> finish(null)).setOnCancelListener(dialog -> finish(null)).create();
        list.setOnItemClickListener((parent,view,position,row) -> { try { folderDialog.dismiss();browseFolder(base,choices.get(position)); } catch(Throwable error) { fail(error); } });folderDialog.show();
    }
    private static void copy(InputStream input,OutputStream output) throws Exception {
        byte[] buffer=new byte[65536];long count=0;int read;
        while((read=input.read(buffer))>=0) { if(read==0)continue;count+=read;if(count>MAX_DOCUMENT_BYTES)throw new IllegalStateException("Document exceeds 64 MiB");output.write(buffer,0,read); }output.flush();
    }
    public static boolean result(Activity owner,int code,int result,Intent data) {
        if(code!=DOCUMENT_REQUEST)return false;if(active==0)return true;
        if(result!=Activity.RESULT_OK || data==null || data.getData()==null) { finish(null);return true; }
        final Uri uri=data.getData();final JSONObject specification=request;final int token=active;
        io.execute(() -> { try {
            File staging=new File(owner.getFilesDir(),"selected-documents");if(!staging.isDirectory() && !staging.mkdirs())throw new IllegalStateException("Cannot create selected-document folder");
            String extension=specification.getString("extension");if(!extension.matches("[A-Za-z0-9]{0,12}"))extension="dat";
            final File file=File.createTempFile("selected-",extension.isEmpty()?".dat":"."+extension,staging);
            final FileObserver[] watcher=new FileObserver[1];
            if(!specification.getBoolean("save")) {
                try(InputStream input=owner.getContentResolver().openInputStream(uri);OutputStream output=new FileOutputStream(file)) { if(input==null)throw new IllegalStateException("Provider returned no file");copy(input,output); }
            } else {
                final android.content.Context context=owner.getApplicationContext();
                FileObserver observer=new FileObserver(file.getPath(),FileObserver.CLOSE_WRITE) {
                    @Override public void onEvent(int event,String path) { if((event&FileObserver.CLOSE_WRITE)==0)return;
                        io.execute(() -> { try(InputStream input=new FileInputStream(file);OutputStream output=context.getContentResolver().openOutputStream(uri,"wt")) { if(output==null)throw new IllegalStateException("Provider returned no output");copy(input,output); } catch(Throwable error) { final Activity current=activity;if(current!=null)current.runOnUiThread(() -> new AlertDialog.Builder(current).setTitle("导出失败").setMessage(error.getMessage()).setPositiveButton(android.R.string.ok,null).show()); } finally { stopWatching();synchronized(exports) { exports.remove(this); } } });
                    }
                };watcher[0]=observer;synchronized(exports) { exports.add(observer); }observer.startWatching();
            }
            owner.runOnUiThread(() -> { if(active==token)finish(file.getPath());else { if(watcher[0]!=null) { watcher[0].stopWatching();synchronized(exports) { exports.remove(watcher[0]); } }file.delete(); } });
        } catch(Throwable error) { fail(error); } });return true;
    }
    private FileChooser() { }
}
