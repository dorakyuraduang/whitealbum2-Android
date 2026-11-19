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
    private String mode="default";
    private int generation;
    // Order matches TextProbeMode and TextProbeCounter in the common JNI interface.
    private static final String[] MODES={"default","reference","run","submission","optimized","page_reference","page",
            "dense","bindings","novel"};
    private static final String[] COUNTERS={"layout_hits","glyph_command_hits","run_hits",
            "surface_commands","surface_hits","surface_tiles","rollover_hits","page_spans","page_glyphs",
            "prepared_geometry_hits","retained_geometry_draws","binding_calls_avoided"};
    private static native boolean nativeSetCandidate(int mode);
    private static native long nativeCounter(int index);
    private void select(int index){
        if(!nativeSetCandidate(index))throw new IllegalStateException("No shared AOT performance control");
        if(!mode.equals(MODES[index]))FrameHud.performanceModeChanged(index==0);
        mode=MODES[index];
    }
    void attach(Activity activity){
        owner=activity;
        activity.registerReceiver(this,new IntentFilter(ACTION),"android.permission.DUMP",null,
                Build.VERSION.SDK_INT>=33?Context.RECEIVER_EXPORTED:0);
    }
    void detach(){
        ++generation;
        try{select(0);}catch(IllegalStateException|LinkageError ignored){}
        if(owner!=null){owner.unregisterReceiver(this);owner=null;}
    }
    @Override public void onReceive(Context context,Intent intent){
        if(!ACTION.equals(intent.getAction()) || !FrameHud.foreground())return;
        try{
            String command=intent.getStringExtra("mode");
            if(command==null)command="snapshot";
            if(!command.equals("snapshot")){
                int index=java.util.Arrays.asList(MODES).indexOf(command);
                if(index<0)throw new IllegalArgumentException("Unknown performance command");
                select(index);
                final int token=++generation;
                if(index!=0)MAIN.postDelayed(()->{
                    if(token==generation && owner!=null){
                        try{select(0);}catch(IllegalStateException|LinkageError ignored){}
                    }
                },30000);
            }
            JSONObject result=FrameHud.performanceSnapshot();
            result.put("version",context.getPackageManager().getPackageInfo(context.getPackageName(),0).versionName);
            result.put("mode",mode);
            for(int i=0;i<COUNTERS.length;++i)result.put(COUNTERS[i],nativeCounter(i));
            result.put("candidate","novel-text-and-texture-bindings");
            Log.i("WA2PERF",result.toString());
        }catch(Exception|LinkageError error){Log.w("WA2PERF","unavailable: "+error.getClass().getSimpleName());}
    }
}
