package org.wa2.port;

import android.app.Activity;
import android.content.Intent;

/** Retained AOT-only widget/file-dialog adapter; native builds replace it. */
final class PortUi implements GameTouchControls.Menu {
    private static final PortUi TOUCH = new PortUi();
    static String[] libraries() { return new String[] { "SDL2", "wa2_android" }; }
    static void attach(Activity owner) { NativeWidgets.attach(owner); FileChooser.attach(owner); }
    static void detach(Activity owner) { FileChooser.detach(); NativeWidgets.detach(owner); }
    static boolean result(Activity owner,int request,int result,Intent data) { return FileChooser.result(owner,request,result,data); }
    static GameTouchControls.Menu touchMenu() { return TOUCH; }
    @Override public void toggle() { NativeWidgets.toggleMenuButton(); }
    @Override public boolean contains(float x,float y) { return NativeWidgets.menuButtonContains(x,y); }
}
