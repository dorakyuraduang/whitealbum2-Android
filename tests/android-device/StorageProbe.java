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
import java.lang.reflect.Field;
import java.util.List;
import java.util.Arrays;
import org.json.JSONObject;
import org.json.JSONArray;

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
        result.put("report_kind",original.optString("report_kind","completed"));result.put("complete",original.optBoolean("complete",true));
        String error=original.optString("error","");result.put("error",error.substring(0,Math.min(4096,error.length())));
        JSONObject process=original.optJSONObject("process");if(process!=null)for(String key:new String[]{"selected_directory_io","save_read_scope","file_io","profile_io","profile_write_io","api_call_counts","api_timing_sample_interval","native_resources","resource_graphics","scheduler","static_cpu_work","frame_work","host_event_pump","native_image_chroma","frame_timing","direct3d9","music_flag_saves"})if(process.has(key))result.put(key,process.get(key));
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
    private JSONObject preparedReads(String path,byte[] expected,int count)throws Exception{
        Method inspect,read;
        try{inspect=documents.getClass().getDeclaredMethod("compatibleReadMetadata",String.class,boolean.class);read=documents.getClass().getDeclaredMethod("compatibleReadOpen",String.class,String.class);}
        catch(NoSuchMethodException absent){return new JSONObject().put("available",false);}
        inspect.setAccessible(true);read.setAccessible(true);long start=System.nanoTime();String[] info=(String[])call(inspect,path,false);long metadataNs=System.nanoTime()-start;
        if(info.length!=5 || !"128".equals(info[3]))throw new IllegalStateException("Invalid resolved file metadata");
        if(info[4].isEmpty())return new JSONObject().put("available",true).put("reference_declined",true);
        long legacyBegin=System.nanoTime();
        for(int n=0;n<count;++n)if(!Arrays.equals(this.read(path),expected))throw new IllegalStateException("Ordinary open content differs");
        long legacyNs=System.nanoTime()-legacyBegin;
        long openNs=0,readNs=0,closeNs=0,total=0,begin=System.nanoTime();
        for(int n=0;n<count;++n){long at=System.nanoTime();int fd=(Integer)call(read,path,info[4]);openNs+=System.nanoTime()-at;if(fd<0)throw new IllegalStateException("Resolved open failed: "+fd);
            InputStream input=new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd));
            try{at=System.nanoTime();byte[] data=bytes(input,1024*1024);readNs+=System.nanoTime()-at;total+=data.length;if(!Arrays.equals(data,expected))throw new IllegalStateException("Resolved read content differs");}
            finally{at=System.nanoTime();input.close();closeNs+=System.nanoTime()-at;}
        }
        long elapsed=System.nanoTime()-begin;int guards=0;Uri source=Uri.parse(info[4]);
        String wrongTree=DocumentsContract.buildDocumentUriUsingTree(DocumentsContract.buildTreeDocumentUri(source.getAuthority(),"__wa2_other_tree__"),"file").toString();
        for(String invalid:new String[]{"","https://example.invalid/wa2","content://invalid.wa2/tree/other/document/file",wrongTree}){
            boolean rejected=false;try{int fd=(Integer)call(read,path,invalid);if(fd>=0)ParcelFileDescriptor.adoptFd(fd).close();}
            catch(java.io.IOException error){rejected=true;}if(!rejected)throw new IllegalStateException("Invalid read reference was accepted");++guards;
        }
        return new JSONObject().put("available",true).put("iterations",count).put("metadata_ns",metadataNs).put("legacy_open_only_ns",legacyNs).put("open_ns",openNs).put("read_ns",readNs).put("close_ns",closeNs).put("elapsed_ns",elapsed).put("bytes",total).put("same_contents",true).put("invalid_reference_guards",guards).put("new_descriptor_each_time",true);
    }
    private static JSONObject privateReads(Context context,byte[] data,int count)throws Exception{
        File fixture=File.createTempFile("wa2-storage-probe-",".ini",context.getCacheDir());
        try{try(FileOutputStream out=new FileOutputStream(fixture)){out.write(data);}
            long begin=System.nanoTime(),total=0;for(int n=0;n<count;++n)try(InputStream input=new FileInputStream(fixture)){total+=bytes(input,1024*1024).length;}
            return new JSONObject().put("iterations",count).put("elapsed_ns",System.nanoTime()-begin).put("bytes",total);
        }finally{fixture.delete();}
    }
    private static int descriptors(){String[] entries=new File("/proc/self/fd").list();return entries==null?-1:entries.length;}
    private JSONObject directoryReads(Uri tree,int count)throws Exception{
        Method read=documents.getClass().getDeclaredMethod("compatibleReadMetadata",String.class,boolean.class);read.setAccessible(true);
        Method light=documents.getClass().getDeclaredMethod("pathChildren",Uri.class),full=documents.getClass().getDeclaredMethod("children",Uri.class);light.setAccessible(true);full.setAccessible(true);
        Uri bank=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
        for(String expected:new String[]{"Documents","Leaf","WHITE ALBUM2"}){
            Uri next=null;
            for(Object entry:(List<?>)call(light,bank)){
                Field name=entry.getClass().getDeclaredField("name"),directory=entry.getClass().getDeclaredField("directory"),uri=entry.getClass().getDeclaredField("uri");name.setAccessible(true);directory.setAccessible(true);uri.setAccessible(true);
                if(expected.equalsIgnoreCase((String)name.get(entry)) && directory.getBoolean(entry)){if(next!=null)throw new IllegalStateException("Ambiguous probe directory");next=(Uri)uri.get(entry);}
            }
            if(next==null)throw new IllegalStateException("No canonical save bank to inspect");bank=next;
        }
        JSONArray timings=new JSONArray();int before=descriptors(),rows=-1;long heapBefore=Runtime.getRuntime().totalMemory()-Runtime.getRuntime().freeMemory();
        for(int n=0;n<count;++n){
            long start=System.nanoTime();String[] entries=(String[])call(read,"Documents/Leaf/WHITE ALBUM2",true);long elapsed=System.nanoTime()-start;
            if(entries.length%5!=0)throw new IllegalStateException("Malformed production directory inventory");
            if(rows>=0 && rows!=entries.length/5)throw new IllegalStateException("Save directory changed during metadata probe");rows=entries.length/5;
            long at=System.nanoTime();int lightRows=((List<?>)call(light,bank)).size();long lightNs=System.nanoTime()-at;
            at=System.nanoTime();int fullRows=((List<?>)call(full,bank)).size();long fullNs=System.nanoTime()-at;
            if(lightRows!=fullRows)throw new IllegalStateException("Directory changed between projections");
            timings.put(new JSONObject().put("iteration",n).put("elapsed_ns",elapsed).put("rows",rows).put("descriptors",descriptors()).put("light_bank_ns",lightNs).put("full_bank_ns",fullNs));
            // Separate immediate repeats from a cold metadata lookup after a
            // pause; this is a finite file component check, not game pressure.
            if(n==3 || n==7)Thread.sleep(1200);
        }
        return new JSONObject().put("iterations",count).put("timings",timings).put("descriptors_before",before).put("descriptors_after",descriptors())
            .put("java_used_bytes_before",heapBefore).put("java_used_bytes_after",Runtime.getRuntime().totalMemory()-Runtime.getRuntime().freeMemory()).put("payload_reads",0);
    }
    @Override public void onStart(){Bundle output=new Bundle();int status=Activity.RESULT_OK;
        try{Context context=getTargetContext();JSONObject result=new JSONObject();result.put("schema","wa2.android-storage-probe.v1");result.put("game_launched",false);result.put("screen_access",false);result.put("selected_directory_written",false);
            File report=new File(context.getFilesDir(),"wa2-runtime.json"),progress=new File(context.getFilesDir(),"wa2-runtime-progress.json");
            result.put("completed_run",summary(report));
            if(progress.isFile() && (!report.isFile() || progress.lastModified()>report.lastModified()))report=progress;
            result.put("last_run",summary(report));
            if(!"report".equals(arguments.getString("mode"))){
                String saved=context.getSharedPreferences("resource-directory",Context.MODE_PRIVATE).getString("tree-uri",null);if(saved==null)throw new IllegalStateException("No selected game directory");Uri tree=Uri.parse(saved);
                requireCleanPublication(context,tree);
                Class<?> type=context.getClassLoader().loadClass("org.wa2.port.GameDocuments");Constructor<?> ctor=type.getDeclaredConstructor(android.content.ContentResolver.class,Uri.class);ctor.setAccessible(true);documents=ctor.newInstance(context.getContentResolver(),tree);
                metadata=type.getDeclaredMethod("compatibleMetadata",String.class,boolean.class);metadata.setAccessible(true);open=type.getDeclaredMethod("compatibleOpen",String.class,int.class,int.class);open.setAccessible(true);
                if("directory".equals(arguments.getString("mode"))){
                    int count=Math.max(1,Math.min(20,Integer.parseInt(arguments.getString("count","12"))));
                    result.put("directory_reads",directoryReads(tree,count));result.put("passed",true);output.putString("stream",result.toString());finish(status,output);return;
                }
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
                if(selected!=null){byte[] data=read(selected);result.put("profile_bytes",data.length);result.put("private_reads",privateReads(context,data,count));result.put("provider_reads",measure(selected,true,count));result.put("prepared_reads",preparedReads(selected,data,count));long begin=System.nanoTime();info(selected);byte[] once=read(selected);result.put("single_provider_read_ns",System.nanoTime()-begin);if(once.length!=data.length)throw new IllegalStateException("Profile changed during probe");}
                for(String missing:new String[]{"default.ini","d3d.ini","save_99.sav"})if(info(missing).length==0){result.put("missing_file_checks",measure(missing,false,count));break;}
            }
            result.put("passed",true);output.putString("stream",result.toString());
        }catch(Throwable error){status=Activity.RESULT_CANCELED;output.putString("stream","STORAGE_PROBE_FAILED: "+error.toString());}
        finish(status,output);
    }
}
