package org.wa2.port;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** Android touch adapter: feeds SDL's existing pointer and keyboard paths. */
final class GameTouchControls {
    interface Forwarder {boolean forward(MotionEvent event);}
    private final Forwarder forwarder;
    private final TouchGestures gestures;
    private final TouchGestures.ControlKey control=new TouchGestures.ControlKey();
    private final Handler handler=new Handler(Looper.getMainLooper());
    private MotionEvent down;
    private float lastX,lastY;
    private long scheduled=-1;
    private boolean longPress;
    private final Runnable timer;
    GameTouchControls(SDLSurface surface,boolean longPress,Forwarder forwarder) {
        if(surface==null)throw new IllegalStateException("SDL input surface unavailable");
        this.forwarder=forwarder;this.longPress=longPress;
        long hold=Math.max(1,ViewConfiguration.getLongPressTimeout());
        gestures=new TouchGestures(longPress,Math.min(300,hold),hold,ViewConfiguration.get(surface.getContext()).getScaledTouchSlop());
        timer=() -> {scheduled=-1;apply(gestures.tick(SystemClock.uptimeMillis()),null);schedule();};
    }
    private void key(int change){if(change>0)SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_CTRL_LEFT);else if(change<0)SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_CTRL_LEFT);}
    private void apply(int effects,MotionEvent event) {
        if((effects&TouchGestures.FAST_UP)!=0)key(control.touch(false));
        if((effects&TouchGestures.CANCEL_NATIVE)!=0 && down!=null){MotionEvent cancel=MotionEvent.obtain(down);cancel.setAction(MotionEvent.ACTION_CANCEL);cancel.setLocation(lastX,lastY);
            try{forwarder.forward(cancel);}finally{cancel.recycle();}}
        if((effects&TouchGestures.FLUSH_DOWN)!=0 && down!=null)forwarder.forward(down);
        if((effects&TouchGestures.FAST_DOWN)!=0)key(control.touch(true));
        if((effects&TouchGestures.MOVE_BEFORE_UP)!=0 && event!=null){MotionEvent move=MotionEvent.obtain(event);move.setAction(MotionEvent.ACTION_MOVE);
            try{forwarder.forward(move);}finally{move.recycle();}}
        if((effects&TouchGestures.FORWARD)!=0 && event!=null)forwarder.forward(event);
        if((effects&TouchGestures.TOGGLE_MENU)!=0)NativeWidgets.toggleMenuButton();
        if(!gestures.active() && down!=null){down.recycle();down=null;}
    }
    private void schedule(){long next=gestures.nextDeadline();if(next==scheduled)return;handler.removeCallbacks(timer);scheduled=next;if(next>=0)handler.postAtTime(timer,next);}
    void longPressEnabled(boolean enabled){longPress=enabled;apply(gestures.enabled(enabled),null);schedule();}
    private void cancelTouch(){handler.removeCallbacks(timer);scheduled=-1;apply(gestures.cancel(),null);}
    void cancel(){cancelTouch();key(control.clear());}
    void detach(){cancel();}
    boolean physicalKey(KeyEvent event){int code=event.getKeyCode();if(code!=KeyEvent.KEYCODE_CTRL_LEFT && code!=KeyEvent.KEYCODE_CTRL_RIGHT)return false;
        int action=event.getAction();if(action!=KeyEvent.ACTION_DOWN && action!=KeyEvent.ACTION_UP)return false;
        key(control.physical(code==KeyEvent.KEYCODE_CTRL_RIGHT,action==KeyEvent.ACTION_DOWN));return true;
    }
    boolean touch(MotionEvent event) {
        int source=event.getSource();
        if((source&InputDevice.SOURCE_TOUCHSCREEN)!=InputDevice.SOURCE_TOUCHSCREEN || (source&InputDevice.SOURCE_MOUSE)==InputDevice.SOURCE_MOUSE)return forwarder.forward(event);
        int action=event.getActionMasked(),index=event.getActionIndex();
        if(action==MotionEvent.ACTION_DOWN){cancelTouch();apply(gestures.enabled(longPress && !NativeWidgets.menuButtonContains(event.getX(),event.getY())),null);
            down=MotionEvent.obtain(event);lastX=event.getX();lastY=event.getY();}
        for(int n=0;n<event.getPointerCount();++n)gestures.pointer(event.getPointerId(n),event.getX(n),event.getY(n));
        if(down!=null){int primary=event.findPointerIndex(down.getPointerId(0));if(primary>=0){lastX=event.getX(primary);lastY=event.getY(primary);}}
        apply(gestures.event(action,event.getPointerId(index),event.getX(index),event.getY(index),event.getPointerCount(),event.getEventTime()),event);
        schedule();return true;
    }
}
