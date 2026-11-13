package org.wa2.port;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import org.json.JSONObject;

/** Shell-only, running-activity measurements. No activity launch or UI input. */
final class PerformanceProbe extends BroadcastReceiver {
    private static final String ACTION="org.wa2.port.PERFORMANCE";
    private static final Handler MAIN=new Handler(Looper.getMainLooper());
    private Activity owner;
    private String mode="optimized";
    private int generation;
    private static native boolean nativeSetCandidate(boolean enabled);
    private static native long nativeCandidateCalls();
    void attach(Activity activity){
        owner=activity;
        activity.registerReceiver(this,new IntentFilter(ACTION),"android.permission.DUMP",null,
                Build.VERSION.SDK_INT>=33?Context.RECEIVER_EXPORTED:0);
    }
    void detach(){
        ++generation;
        try{nativeSetCandidate(true);}catch(LinkageError ignored){}
        if(owner!=null){owner.unregisterReceiver(this);owner=null;}
    }
    @Override public void onReceive(Context context,Intent intent){
        if(!ACTION.equals(intent.getAction()) || !FrameHud.foreground())return;
        try{
            String command=intent.getStringExtra("mode");
            if(command==null)command="snapshot";
            if(command.equals("reference") || command.equals("reference_hold") || command.equals("optimized")){
                if(!nativeSetCandidate(command.equals("optimized")))throw new IllegalStateException("No shared AOT performance control");
                mode=command.equals("optimized")?"optimized":"reference";final int token=++generation;
                if(!command.equals("reference_hold"))MAIN.postDelayed(()->{
                    if(token==generation){nativeSetCandidate(true);mode="optimized";}
                },30000);
            }else if(!command.equals("snapshot"))throw new IllegalArgumentException("Unknown performance command");
            JSONObject result=FrameHud.performanceSnapshot();
            result.put("version",context.getPackageManager().getPackageInfo(context.getPackageName(),0).versionName);
            result.put("mode",mode);
            result.put("candidate_calls",nativeCandidateCalls());
            result.put("candidate","immutable-glyph-buffers");
            Log.i("WA2PERF",result.toString());
        }catch(Exception|LinkageError error){Log.w("WA2PERF","unavailable: "+error.getClass().getSimpleName());}
    }
}
