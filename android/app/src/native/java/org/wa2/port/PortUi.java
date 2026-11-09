package org.wa2.port;

import android.app.Activity;
import android.content.Intent;
import android.view.Gravity;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import android.widget.Toast;

/** Native-only Android presentation. Menu policy/actions live in common C++. */
final class PortUi implements GameTouchControls.Menu {
    private static final PortUi TOUCH = new PortUi();
    private static Activity activity;
    private static TextView button;
    private static PopupMenu popup;
    private static boolean hidden;
    private static final int[] location = new int[2];
    private static native String[] nativeLabels();
    private static native long nativeState();
    private static native boolean nativeChoose(int action);

    static String[] libraries() { return new String[] { "SDL2", "wa2_native_android" }; }
    static GameTouchControls.Menu touchMenu() { return TOUCH; }
    static boolean result(Activity owner,int request,int result,Intent data) { return false; }
    static void attach(Activity owner) {
        if(owner==null)throw new IllegalArgumentException("Missing native menu activity");
        if(activity!=null)detach(activity);
        activity=owner;hidden=!LaunchSettings.showMenu(owner);
        button=new TextView(owner);button.setText("\u22ee");button.setTextSize(28);button.setGravity(Gravity.CENTER);
        button.setTextColor(0xffeeeeee);button.setBackgroundColor(0x70404040);button.setContentDescription("\u6e38\u620f\u83dc\u5355");
        int size=Math.round(44*owner.getResources().getDisplayMetrics().density);
        FrameLayout.LayoutParams layout=new FrameLayout.LayoutParams(size,size,Gravity.TOP|Gravity.RIGHT);
        owner.addContentView(button,layout);
        button.setVisibility(hidden?View.GONE:View.VISIBLE);
        button.setOnClickListener(view -> open(owner,view));
    }
    private static void open(Activity owner,View anchor) {
        if(owner!=activity || anchor!=button || hidden)return;
        if(popup!=null)popup.dismiss();
        String[] labels=nativeLabels();long state=nativeState();
        if(labels==null || labels.length>31)throw new IllegalStateException("Invalid common menu description");
        PopupMenu next=new PopupMenu(owner,anchor);
        for(int n=0;n<labels.length;++n){MenuItem item=next.getMenu().add(0,n,n,labels[n]);item.setEnabled((state&(1L<<n))!=0);
            if((state&(1L<<(n+32)))!=0)item.setCheckable(true).setChecked(true);}
        next.setOnMenuItemClickListener(item -> {
            if(owner!=activity || anchor!=button)return true;
            if(!nativeChoose(item.getItemId()))Toast.makeText(owner,"\u6e38\u620f\u6682\u65f6\u65e0\u6cd5\u6267\u884c\u6b64\u64cd\u4f5c",Toast.LENGTH_SHORT).show();
            return true;
        });
        next.setOnDismissListener(menu -> {if(popup==next)popup=null;});popup=next;next.show();
    }
    static void detach(Activity owner) {
        if(owner!=activity)return;
        activity=null;if(popup!=null){popup.dismiss();popup=null;}
        if(button!=null){button.setOnClickListener(null);if(button.getParent() instanceof ViewGroup)((ViewGroup)button.getParent()).removeView(button);button=null;}
        hidden=false;
    }
    @Override public void toggle() {hidden=!hidden;if(popup!=null)popup.dismiss();if(button!=null)button.setVisibility(hidden?View.GONE:View.VISIBLE);}
    @Override public boolean contains(float x,float y) {
        if(button==null || !button.isShown())return false;button.getLocationInWindow(location);
        return x>=location[0] && x<location[0]+button.getWidth() && y>=location[1] && y<location[1]+button.getHeight();
    }
}
