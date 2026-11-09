package org.wa2.probe;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import org.json.JSONObject;

/** Read-only provider timing in the target UID. No Activity, game or screen. */
public final class StorageProbe extends Instrumentation {
    private static native String nativeMeasure(String directory,Object documents,String profileName,boolean optimized,boolean profile,int count);
    private void progress(String value){Bundle output=new Bundle();output.putString("stream",value+"\n");sendStatus(1,output);}
    // The production adapter normally recovers publication sidecars even on
    // reads. Decline a test requiring that recovery instead of modifying them.
    private static void requireCleanPublication(Context context,Uri tree)throws Exception{
        Uri parent=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
        String[] chain={"Documents","Leaf","WHITE ALBUM2"};
        for(int depth=0;depth<=chain.length;++depth){Uri next=null;
            Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(parent));
            String[] fields={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE};
            try(Cursor cursor=context.getContentResolver().query(children,fields,null,null,null)){
                if(cursor==null)throw new IllegalStateException("Cannot inspect probe directory");int count=0;
                while(cursor.moveToNext()){if(++count>10000)throw new IllegalStateException("Probe directory exceeds bound");String name=cursor.getString(1);
                    if(name.endsWith(".wa2-new") || name.endsWith(".wa2-old"))throw new IllegalStateException("Pending file publication: exit the game normally before probing");
                    if(depth<chain.length && name.equalsIgnoreCase(chain[depth]) && DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2)))next=DocumentsContract.buildDocumentUriUsingTree(tree,cursor.getString(0));
                }
            }
            if(next==null)break;parent=next;
        }
    }
    private Bundle arguments;
    private Object documents;
    private Method metadata,open;
    @Override public void onCreate(Bundle args){arguments=args==null?new Bundle():args;start();}
    private Object call(Method method,Object...args)throws Exception{
        try{return method.invoke(documents,args);}catch(InvocationTargetException error){Throwable cause=error.getCause();if(cause instanceof Exception)throw (Exception)cause;throw error;}
    }
    private static byte[] bytes(InputStream input,int maximum)throws Exception{
        ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int count;
        while((count=input.read(buffer))>=0){if(out.size()+count>maximum)throw new IllegalStateException("Probe input exceeds bound");out.write(buffer,0,count);}return out.toByteArray();
    }
    private String[] info(String name)throws Exception{return (String[])call(metadata,name,false);}
    private String findProfile()throws Exception{
        for(String path:new String[]{"system.ini","documents/leaf/white album2/system.ini","d3d.ini","default.ini"}){String[] found=info(path);if(found.length==4 && "128".equals(found[3]) && Long.parseLong(found[1])<=1024*1024)return path;}
        return null;
    }
    private byte[] read(String name)throws Exception{
        int fd=(Integer)call(open,name,1,3);if(fd<0)throw new IllegalStateException("Read-only open returned "+fd);
        try(InputStream input=new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd))){return bytes(input,1024*1024);}
    }
    private static JSONObject summary(File file)throws Exception{
        JSONObject result=new JSONObject();result.put("available",file.isFile());if(!file.isFile())return result;
        JSONObject original;try(InputStream input=new FileInputStream(file)){original=new JSONObject(new String(bytes(input,2*1024*1024),"UTF-8"));}
        result.put("host_release",original.opt("host_release"));result.put("modified_ms",file.lastModified());
        String error=original.optString("error","");result.put("error",error.substring(0,Math.min(4096,error.length())));
        JSONObject process=original.optJSONObject("process");if(process!=null)for(String key:new String[]{"selected_directory_io","save_read_scope","file_io","profile_io","native_resources","resource_graphics","scheduler","static_cpu_work"})if(process.has(key))result.put(key,process.get(key));
        return result;
    }
    private JSONObject measure(String path,boolean present,int count)throws Exception{
        long metadataNs=0,openNs=0,readNs=0,closeNs=0,totalBytes=0,maximum=0;long begin=System.nanoTime();
        for(int n=0;n<count;++n){long start=System.nanoTime();String[] found=info(path);metadataNs+=System.nanoTime()-start;
            if((found.length!=0)!=present)throw new IllegalStateException("Profile changed during read-only probe");
            long at=System.nanoTime();int fd=(Integer)call(open,path,1,3);openNs+=System.nanoTime()-at;
            if(present){if(fd<0)throw new IllegalStateException("Cannot open probed profile: "+fd);
                ParcelFileDescriptor.AutoCloseInputStream input=new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd));
                try{at=System.nanoTime();byte[] data=bytes(input,1024*1024);readNs+=System.nanoTime()-at;totalBytes+=data.length;}
                finally{at=System.nanoTime();input.close();closeNs+=System.nanoTime()-at;}
            }else if(fd>=0){ParcelFileDescriptor.adoptFd(fd).close();throw new IllegalStateException("Missing profile appeared during probe");}
            maximum=Math.max(maximum,System.nanoTime()-start);
        }
        return new JSONObject().put("iterations",count).put("metadata_ns",metadataNs).put("open_ns",openNs).put("read_ns",readNs).put("close_ns",closeNs).put("elapsed_ns",System.nanoTime()-begin).put("maximum_iteration_ns",maximum).put("bytes",totalBytes).put("present",present);
    }
    private static JSONObject privateReads(Context context,byte[] data,int count)throws Exception{
        File fixture=File.createTempFile("wa2-storage-probe-",".ini",context.getCacheDir());
        try{try(FileOutputStream out=new FileOutputStream(fixture)){out.write(data);}
            long begin=System.nanoTime(),total=0;for(int n=0;n<count;++n)try(InputStream input=new FileInputStream(fixture)){total+=bytes(input,1024*1024).length;}
            return new JSONObject().put("iterations",count).put("elapsed_ns",System.nanoTime()-begin).put("bytes",total);
        }finally{fixture.delete();}
    }
    @Override public void onStart(){Bundle output=new Bundle();int status=Activity.RESULT_OK;
        try{Context context=getTargetContext();JSONObject result=new JSONObject();result.put("schema","wa2.android-storage-probe.v1");result.put("game_launched",false);result.put("screen_access",false);result.put("selected_directory_written",false);
            result.put("last_run",summary(new File(context.getFilesDir(),"wa2-runtime.json")));
            if(!"report".equals(arguments.getString("mode"))){
                String saved=context.getSharedPreferences("resource-directory",Context.MODE_PRIVATE).getString("tree-uri",null);if(saved==null)throw new IllegalStateException("No selected game directory");Uri tree=Uri.parse(saved);
                requireCleanPublication(context,tree);
                Class<?> type=context.getClassLoader().loadClass("org.wa2.port.GameDocuments");Constructor<?> ctor=type.getDeclaredConstructor(android.content.ContentResolver.class,Uri.class);ctor.setAccessible(true);documents=ctor.newInstance(context.getContentResolver(),tree);
                metadata=type.getDeclaredMethod("compatibleMetadata",String.class,boolean.class);metadata.setAccessible(true);open=type.getDeclaredMethod("compatibleOpen",String.class,int.class,int.class);open.setAccessible(true);
                if("native".equals(arguments.getString("mode"))){
                    System.loadLibrary("wa2_storage_probe");
                    File scratch=new File(context.getCacheDir(),"wa2-headless-probe");if(!scratch.isDirectory() && !scratch.mkdir())throw new IllegalStateException("Cannot create private probe scratch directory");
                    int count=Math.max(1,Math.min(100,Integer.parseInt(arguments.getString("count","10"))));
                    String profileName=findProfile();result.put("existing_profile_used",profileName!=null);if(profileName==null)profileName="SYSTEM.ini";
                    try{for(boolean profile:new boolean[]{true,false}){
                        progress(profile?"native_profile_baseline":"native_save_baseline");
                        JSONObject baseline=new JSONObject(nativeMeasure(scratch.getAbsolutePath(),documents,profileName,false,profile,count));
                        progress("baseline_ms="+baseline.getLong("elapsed_ns")/1000000);
                        JSONObject optimized=new JSONObject(nativeMeasure(scratch.getAbsolutePath(),documents,profileName,true,profile,count));
                        progress("optimized_ms="+optimized.getLong("elapsed_ns")/1000000);
                        if(!baseline.getString("result_checksum").equals(optimized.getString("result_checksum")))throw new IllegalStateException("Selected files changed or static results differ");
                        result.put(profile?"native_profiles":"native_save_inventory",new JSONObject().put("baseline",baseline).put("optimized",optimized));
                    }}finally{scratch.delete();}
                    result.put("passed",true);output.putString("stream",result.toString());finish(status,output);return;
                }
                int count=Math.max(1,Math.min(100,Integer.parseInt(arguments.getString("count","20"))));String selected=findProfile();
                result.put("profile_found",selected!=null);
                if(selected!=null){byte[] data=read(selected);result.put("profile_bytes",data.length);result.put("private_reads",privateReads(context,data,count));result.put("provider_reads",measure(selected,true,count));long begin=System.nanoTime();info(selected);byte[] once=read(selected);result.put("single_provider_read_ns",System.nanoTime()-begin);if(once.length!=data.length)throw new IllegalStateException("Profile changed during probe");}
                for(String missing:new String[]{"default.ini","d3d.ini","save_99.sav"})if(info(missing).length==0){result.put("missing_file_checks",measure(missing,false,count));break;}
            }
            result.put("passed",true);output.putString("stream",result.toString());
        }catch(Throwable error){status=Activity.RESULT_CANCELED;output.putString("stream","STORAGE_PROBE_FAILED: "+error.toString());}
        finish(status,output);
    }
}
