package org.wa2.port;

import android.os.Bundle;
import android.content.Intent;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;

public final class Wa2Activity extends SDLActivity {
    private void finishNormally() { super.finish(); }
    @Override public void finish() {
        if (!ResourceDirectory.finishGame(this, this::finishNormally)) super.finish();
    }
    @Override protected String[] getLibraries() { return new String[] { "SDL2", "wa2_android" }; }
    private static native void nativeResetFrameMetrics();
    private static void resetFrameMetricsIfLoaded() {
        // SDL itself reports native-library loading errors. An optional HUD
        // must also be safe while that error screen is being closed.
        try { nativeResetFrameMetrics(); } catch (UnsatisfiedLinkError ignored) { }
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (SDLActivity.mBrokenLibraries) return;
        ResourceDirectory.attach(this);
        NativeWidgets.attach(this);
        FileChooser.attach(this);
        FrameHud.install(this);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.preferredRefreshRate = 120f;
        getWindow().setAttributes(attributes);
    }
    @Override protected void onResume() {
        super.onResume();
        resetFrameMetricsIfLoaded();
        FrameHud.setResumed(true);
    }
    @Override protected void onPause() {
        FrameHud.setResumed(false);
        resetFrameMetricsIfLoaded();
        super.onPause();
    }
    @Override protected void onDestroy() {
        // Release a game thread waiting for directory selection before SDL
        // joins it in its own onDestroy handler.
        ResourceDirectory.detach(this);
        FileChooser.detach();
        NativeWidgets.detach(this);
        FrameHud.detach();
        super.onDestroy();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if (!ResourceDirectory.result(this, request, result, data) && !FileChooser.result(this, request, result, data))
            super.onActivityResult(request, result, data);
    }
}
