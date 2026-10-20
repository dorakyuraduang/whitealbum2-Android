package org.wa2.port;

/** Touch-to-device-input policy; no Android views, game scripts or frame loop. */
final class TouchGestures {
    static final int DOWN=0,UP=1,MOVE=2,CANCEL=3,POINTER_DOWN=5,POINTER_UP=6;
    static final int FORWARD=1,FLUSH_DOWN=2,CANCEL_NATIVE=4,FAST_DOWN=8,FAST_UP=16,TOGGLE_MENU=32,MOVE_BEFORE_UP=64;
    private final long tapMillis,holdMillis;
    private final float slopSquared;
    private boolean enabled,active,pending,forwarding,multi,moved,fast,twoTap;
    private int firstId,secondId;
    private float firstX,firstY,secondX,secondY;
    private long started;
    TouchGestures(boolean enabled,long tapMillis,long holdMillis,float slop) {
        if(tapMillis<1 || holdMillis<tapMillis || holdMillis>60000 || !Float.isFinite(slop) || slop<0 || slop>4096)
            throw new IllegalArgumentException("Invalid touch gesture limits");
        this.enabled=enabled;this.tapMillis=tapMillis;this.holdMillis=holdMillis;slopSquared=slop*slop;
    }
    boolean active(){return active;}
    private long deadline(long delay){return started>Long.MAX_VALUE-delay?Long.MAX_VALUE:started+delay;}
    long nextDeadline(){return active && !multi && !moved && enabled && !fast?deadline(holdMillis):-1;}
    void pointer(int id,float x,float y) {
        if(!active)return;
        float dx,dy;
        if(id==firstId){dx=x-firstX;dy=y-firstY;}
        else if(multi && id==secondId){dx=x-secondX;dy=y-secondY;}
        else return;
        if(!Float.isFinite(dx) || !Float.isFinite(dy) || dx*dx+dy*dy>slopSquared){moved=true;twoTap=false;}
    }
    int cancel(){int effects=(forwarding?CANCEL_NATIVE:0)|(fast?FAST_UP:0);active=pending=forwarding=multi=moved=fast=twoTap=false;firstId=secondId=-1;return effects;}
    int enabled(boolean value){if(enabled==value)return 0;int effects=cancel();enabled=value;return effects;}
    int tick(long now) {
        if(!active || multi || moved || now<started)return 0;
        int effects=0;
        if(enabled && !fast && now>=deadline(holdMillis)){
            if(pending){pending=false;forwarding=true;effects|=FLUSH_DOWN;}fast=true;effects|=FAST_DOWN;
        }return effects;
    }
    int event(int action,int id,float x,float y,int count,long time) {
        if(action==CANCEL)return cancel();
        if(action==DOWN){int effects=cancel();active=pending=true;firstId=id;firstX=x;firstY=y;started=time;return effects;}
        if(!active)return 0;
        if(time<started)return cancel();
        if(action==POINTER_DOWN || (action==POINTER_UP && !multi)){
            int effects=(forwarding?CANCEL_NATIVE:0)|(fast?FAST_UP:0);
            boolean eligible=action==POINTER_DOWN && !multi && count==2 && pending && !moved && time-started<tapMillis;
            if(!multi){secondId=id;secondX=x;secondY=y;}
            multi=true;pending=forwarding=fast=false;twoTap=eligible;return effects;
        }
        if(action==UP){int effects;
            if(multi)effects=twoTap && !moved && count==1 && time-started<tapMillis?TOGGLE_MENU:0;
            else effects=(pending?FLUSH_DOWN:0)|((pending || forwarding)?FORWARD:0)|(fast?FAST_UP:0)|((pending && moved)?MOVE_BEFORE_UP:0);
            cancel();return effects;
        }
        if(multi)return 0;
        if(action==MOVE){int effects=0;
            if(moved){if(fast){fast=false;effects|=FAST_UP;}if(pending){pending=false;forwarding=true;effects|=FLUSH_DOWN;}}
            else effects|=tick(time);
            return effects|(forwarding?FORWARD:0);
        }return 0;
    }
    /** One logical Ctrl key owns physical-left, physical-right and touch holds. */
    static final class ControlKey {
        private boolean left,right,touch,sent;
        private int change(){boolean down=left || right || touch;if(down==sent)return 0;sent=down;return down?1:-1;}
        int touch(boolean down){touch=down;return change();}
        int physical(boolean isRight,boolean down){if(isRight)right=down;else left=down;return change();}
        int clear(){left=right=touch=false;return change();}
    }
}
