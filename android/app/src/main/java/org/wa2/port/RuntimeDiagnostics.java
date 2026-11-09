package org.wa2.port;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Process;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import org.json.JSONObject;

/** Copy bounded completed/progress counters; never reads the screen. */
final class RuntimeDiagnostics {
    static void copy(Activity activity,TextView status){
        status.setText("正在读取上次运行诊断…");
        new Thread(()->{
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            String result=null,problem=null;
            try{
                File file=RuntimeReportFile.latest(activity.getFilesDir());
                if(!file.isFile())throw new IOException("还没有运行诊断。请读档后从游戏菜单正常退出，再复制诊断。");
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();
                try(FileInputStream input=new FileInputStream(file)){
                    byte[] buffer=new byte[8192];int count;
                    while((count=input.read(buffer))>=0){if(bytes.size()+count>2*1024*1024)throw new IOException("运行诊断超过读取大小限制");bytes.write(buffer,0,count);}
                }
                JSONObject original=new JSONObject(new String(bytes.toByteArray(),StandardCharsets.UTF_8));
                JSONObject report=new JSONObject();report.put("host_release",original.opt("host_release"));report.put("report_modified_ms",file.lastModified());
                report.put("report_kind",original.optString("report_kind","completed"));report.put("complete",original.optBoolean("complete",true));
                String error=original.optString("error","");report.put("error",error.substring(0,Math.min(error.length(),4096)));
                JSONObject process=original.optJSONObject("process");
                if(process==null)throw new IOException("运行诊断格式不完整");
                for(String key:new String[]{"static_function_entries","api_calls","selected_directory_io","save_read_scope","file_io","profile_io","native_resources","resource_graphics","scheduler","static_cpu_work"})
                    if(process.has(key))report.put(key,process.get(key));
                result=report.toString(2);if(result.length()>65536)throw new IOException("诊断摘要超过复制大小限制");
            }catch(Exception error){problem=error.getMessage()==null?"无法读取上次运行诊断":error.getMessage();}
            final String text=result,failure=problem;
            activity.runOnUiThread(()->{
                if(activity.isFinishing() || activity.isDestroyed())return;
                if(failure!=null){status.setText(failure);return;}
                try{
                    ClipboardManager clipboard=(ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
                    if(clipboard==null)throw new IllegalStateException("剪贴板不可用");
                    clipboard.setPrimaryClip(ClipData.newPlainText("WA2 运行诊断",text));status.setText("已复制上次运行诊断，可粘贴反馈。只包含计数和耗时，不含画面或存档内容。");
                }catch(RuntimeException error){status.setText("无法复制运行诊断："+error.getMessage());}
            });
        },"WA2 diagnostics").start();
    }
    private RuntimeDiagnostics(){}
}
