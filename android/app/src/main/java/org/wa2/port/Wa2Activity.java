package org.wa2.port;

import android.os.Bundle;
import android.content.Intent;
import android.content.Context;
import android.content.res.Configuration;
import android.view.WindowManager;
import android.view.KeyEvent;
import android.view.MotionEvent;
import org.libsdl.app.SDLActivity;

public final class Wa2Activity extends SDLActivity {
    private GameTouchControls touchControls;
    @Override protected void attachBaseContext(Context base){
        Configuration game=new Configuration(base.getResources().getConfiguration());
        game.setLayoutDirection(java.util.Locale.ROOT);
        super.attachBaseContext(base.createConfigurationContext(game));
    }
    private void finishNormally() { super.finish(); }
    @Override public void finish() {
        if (!ResourceDirectory.finishGame(this, this::finishNormally)) super.finish();
    }
    @Override protected String[] getLibraries() { return PortUi.libraries(); }
    @Override protected String[] getArguments() { return LaunchSettings.gameArguments(this); }
    @Override protected void resumeNativeThread() {
        // A restored task must retain its verified folder/profile pairing.
        // If it cannot, return through the launcher before SDL_main can run.
        if(isFinishing() || !LaunchSettings.sessionValid(this))return;
        super.resumeNativeThread();
    }
    private static native void nativeResetFrameMetrics();
    private static void resetFrameMetricsIfLoaded() {
        // SDL itself reports native-library loading errors. An optional HUD
        // must also be safe while that error screen is being closed.
        try { nativeResetFrameMetrics(); } catch (UnsatisfiedLinkError ignored) { }
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Launcher RTL is a presentation preference; the original game UI
        // keeps its source coordinate direction and common input adapter.
        getWindow().getDecorView().setLayoutDirection(android.view.View.LAYOUT_DIRECTION_LTR);
        if (SDLActivity.mBrokenLibraries) return;
        if(!LaunchSettings.sessionValid(this)){startActivity(new Intent(this,LauncherActivity.class));finishNormally();return;}
        ResourceDirectory.attach(this);
        PortUi.attach(this);
        touchControls=new GameTouchControls(mSurface,LaunchSettings.longPressFastForward(this),this::forwardTouch,PortUi.touchMenu());
        FrameHud.install(this);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.preferredRefreshRate = 120f;
        getWindow().setAttributes(attributes);
    }
    @Override protected void onResume() {
        super.onResume();
        if(touchControls!=null)touchControls.longPressEnabled(LaunchSettings.longPressFastForward(this));
        resetFrameMetricsIfLoaded();
        FrameHud.setResumed(true);
    }
    @Override protected void onPause() {
        if(touchControls!=null)touchControls.cancel();
        FrameHud.setResumed(false);
        resetFrameMetricsIfLoaded();
        super.onPause();
    }
    @Override protected void onDestroy() {
        if(touchControls!=null){touchControls.detach();touchControls=null;}
        // Release a game thread waiting for directory selection before SDL
        // joins it in its own onDestroy handler.
        ResourceDirectory.detach(this);
        PortUi.detach(this);
        FrameHud.detach();
        super.onDestroy();
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        if(!focused && touchControls!=null)touchControls.cancel();
        super.onWindowFocusChanged(focused);
    }
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if(!SDLActivity.mBrokenLibraries && touchControls!=null && touchControls.physicalKey(event))return true;
        return super.dispatchKeyEvent(event);
    }
    private boolean forwardTouch(MotionEvent event){return super.dispatchTouchEvent(event);}
    @Override public boolean dispatchTouchEvent(MotionEvent event){
        if(!SDLActivity.mBrokenLibraries && touchControls!=null)return touchControls.touch(event);
        return super.dispatchTouchEvent(event);
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if (!ResourceDirectory.result(this, request, result, data) && !PortUi.result(this, request, result, data))
            super.onActivityResult(request, result, data);
    }
}
