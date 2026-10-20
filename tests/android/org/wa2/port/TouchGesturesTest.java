package org.wa2.port;

import java.nio.file.Files;
import java.nio.file.Path;
import static org.wa2.port.TouchGestures.*;

/** Synthetic pointer/clock traces against the production policy, without Views. */
public final class TouchGesturesTest {
    static int checks,scenarios,twoTaps,cancels,keyTraces;
    static void check(boolean value){if(!value)throw new AssertionError("Touch input check " + checks);++checks;}
    static void effects(int actual,int expected){if(actual!=expected)throw new AssertionError("Touch effects: expected "+expected+", got "+actual+", check "+checks);++checks;}
    static TouchGestures create(boolean enabled){return new TouchGestures(enabled,300,500,8);}
    static int event(TouchGestures g,int action,int id,int count,long time){return g.event(action,id,id*10,0,count,time);}
    static void begin(TouchGestures g,int id,long time){effects(event(g,DOWN,id,1,time),0);check(g.active());}
    static void ended(TouchGestures g){check(!g.active() && g.nextDeadline()==-1);effects(g.tick(10000000),0);effects(g.cancel(),0);++scenarios;}
    public static void main(String[] args) throws Exception {
        TouchGestures g=create(true);begin(g,7,0);check(g.nextDeadline()==500);effects(event(g,UP,7,1,90),FLUSH_DOWN|FORWARD);ended(g);
        g=create(true);begin(g,7,1000);effects(g.tick(1299),0);effects(g.tick(1300),0);check(g.nextDeadline()==1500);
        effects(g.tick(1499),0);effects(g.tick(1500),FLUSH_DOWN|FAST_DOWN);check(g.nextDeadline()==-1);effects(g.tick(7000),0);effects(event(g,UP,7,1,7100),FORWARD|FAST_UP);ended(g);
        g=create(false);begin(g,2,0);effects(g.tick(300),0);check(g.nextDeadline()==-1);effects(g.tick(9000),0);effects(event(g,UP,2,1,9100),FLUSH_DOWN|FORWARD);ended(g);
        // Lifting after a delayed UI timer cannot synthesize a skip-key tap.
        g=create(true);begin(g,3,0);effects(event(g,UP,3,1,800),FLUSH_DOWN|FORWARD);ended(g);
        // Coalesced input may deliver a moved UP without an intermediate MOVE.
        g=create(false);begin(g,2,0);g.pointer(2,40,0);effects(event(g,UP,2,1,90),FLUSH_DOWN|MOVE_BEFORE_UP|FORWARD);ended(g);
        g=create(true);begin(g,2,0);g.pointer(2,29,0);effects(event(g,MOVE,2,1,30),FLUSH_DOWN|FORWARD);check(g.nextDeadline()==-1);
        effects(g.tick(9000),0);effects(event(g,UP,2,1,9100),FORWARD);ended(g);
        g=create(true);begin(g,2,0);effects(g.tick(300),0);effects(g.tick(500),FLUSH_DOWN|FAST_DOWN);g.pointer(2,29,0);
        effects(event(g,MOVE,2,1,510),FORWARD|FAST_UP);effects(g.tick(1000),0);effects(event(g,UP,2,1,1100),FORWARD);ended(g);
        g=create(true);begin(g,2,0);g.pointer(2,28,0);effects(event(g,MOVE,2,1,250),0);effects(g.tick(300),0);effects(g.tick(500),FLUSH_DOWN|FAST_DOWN);
        effects(event(g,UP,2,1,510),FORWARD|FAST_UP);ended(g); // Exactly at touch slop is still a hold.
        for(boolean enabled:new boolean[]{false,true})for(int first:new int[]{0,7,31})for(boolean firstUp:new boolean[]{false,true}){
            g=create(enabled);int second=(first+3)%32;begin(g,first,1000);effects(event(g,POINTER_DOWN,second,2,1040),0);check(g.nextDeadline()==-1);
            effects(event(g,POINTER_UP,firstUp?first:second,2,1080),0);effects(event(g,UP,firstUp?second:first,1,1100),TOGGLE_MENU);++twoTaps;ended(g);
        }
        // The whole pair is swallowed after either pointer moves or stays down too long.
        g=create(true);begin(g,1,0);effects(event(g,POINTER_DOWN,7,2,50),0);g.pointer(7,79,0);
        effects(event(g,MOVE,7,2,100),0);effects(event(g,POINTER_UP,7,2,120),0);effects(event(g,UP,1,1,150),0);ended(g);
        g=create(true);begin(g,1,0);effects(event(g,POINTER_DOWN,7,2,50),0);effects(g.tick(9000),0);
        effects(event(g,POINTER_UP,7,2,9100),0);effects(event(g,UP,1,1,9200),0);ended(g);
        // A third contact or a replacement second contact cannot toggle the menu.
        for(boolean replacement:new boolean[]{false,true}){
            g=create(true);begin(g,1,0);effects(event(g,POINTER_DOWN,7,2,20),0);
            if(replacement)effects(event(g,POINTER_UP,7,2,30),0);
            effects(event(g,POINTER_DOWN,9,replacement?2:3,40),0);effects(event(g,POINTER_UP,9,replacement?2:3,70),0);
            if(!replacement)effects(event(g,POINTER_UP,7,2,80),0);effects(event(g,UP,1,1,100),0);ended(g);
        }
        // Starting another finger during an established pointer/skip hold releases both.
        g=create(true);begin(g,1,0);g.pointer(1,19,0);effects(event(g,MOVE,1,1,30),FLUSH_DOWN|FORWARD);effects(event(g,POINTER_DOWN,7,2,310),CANCEL_NATIVE);
        effects(event(g,POINTER_UP,7,2,320),0);effects(event(g,UP,1,1,330),0);ended(g);
        g=create(true);begin(g,1,0);effects(g.tick(500),FLUSH_DOWN|FAST_DOWN);effects(event(g,POINTER_DOWN,7,2,510),CANCEL_NATIVE|FAST_UP);
        effects(event(g,POINTER_UP,7,2,520),0);effects(event(g,UP,1,1,530),0);ended(g);
        // Pause/focus loss/cancel at each stage must never produce a click or toggle.
        for(int stage=0;stage<4;++stage){g=create(true);begin(g,1,0);int expected=0;
            if(stage==1){g.pointer(1,19,0);effects(event(g,MOVE,1,1,30),FLUSH_DOWN|FORWARD);expected=CANCEL_NATIVE;}
            if(stage==2){effects(g.tick(500),FLUSH_DOWN|FAST_DOWN);expected=CANCEL_NATIVE|FAST_UP;}
            if(stage==3)effects(event(g,POINTER_DOWN,7,2,20),0);
            effects(event(g,CANCEL,1,1,800),expected);++cancels;ended(g);
        }
        g=create(true);begin(g,1,0);effects(g.enabled(true),0);effects(g.tick(500),FLUSH_DOWN|FAST_DOWN);
        effects(g.enabled(false),CANCEL_NATIVE|FAST_UP);++cancels;ended(g);begin(g,2,1000);effects(g.tick(1600),0);effects(event(g,UP,2,1,1700),FLUSH_DOWN|FORWARD);ended(g);
        g=create(true);begin(g,1,10);effects(event(g,UP,1,1,9),0);ended(g);
        // Repeated mixed gestures always end with a released logical pointer/key.
        g=create(true);
        for(int n=0;n<256;++n){long at=n*1000L;begin(g,1,at);
            switch(n%4){
                case 0:effects(event(g,UP,1,1,at+50),FLUSH_DOWN|FORWARD);break;
                case 1:effects(event(g,POINTER_DOWN,7,2,at+20),0);effects(event(g,POINTER_UP,7,2,at+50),0);effects(event(g,UP,1,1,at+80),TOGGLE_MENU);++twoTaps;break;
                case 2:effects(g.tick(at+500),FLUSH_DOWN|FAST_DOWN);effects(event(g,UP,1,1,at+550),FORWARD|FAST_UP);break;
                default:effects(g.tick(at+500),FLUSH_DOWN|FAST_DOWN);effects(g.cancel(),CANCEL_NATIVE|FAST_UP);++cancels;break;
            }ended(g);
        }
        // Left Ctrl, right Ctrl and touch share one key, in all activation/release orders.
        int[][] orders={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(int mask=0;mask<8;++mask)for(int[] order:orders)for(int[] release:orders){TouchGestures.ControlKey key=new TouchGestures.ControlKey();int held=0;
            for(int source:order)if((mask&(1<<source))!=0){int before=held;held|=1<<source;effects(key(key,source,true),before==0?1:0);effects(key(key,source,true),0);}
            for(int source:release)if((mask&(1<<source))!=0){held&=~(1<<source);effects(key(key,source,false),held==0?-1:0);}
            effects(key.clear(),0);effects(key.touch(false),0);++keyTraces;
        }
        TouchGestures.ControlKey key=new TouchGestures.ControlKey();effects(key.physical(false,true),1);effects(key.touch(true),0);effects(key.touch(false),0);effects(key.physical(false,false),-1);
        effects(key.touch(true),1);effects(key.physical(true,true),0);effects(key.touch(false),0);effects(key.clear(),-1);effects(key.clear(),0);
        check(twoTaps==76 && cancels==69 && keyTraces==288);
        Files.writeString(Path.of(args[0]),"{\"schema\":\"wa2.touch-gestures.v1\",\"checks\":"+checks+",\"gesture_traces\":"+scenarios+",\"two_finger_taps\":"+twoTaps+",\"cancel_release_cases\":"+cancels+",\"control_source_traces\":"+keyTraces
            +",\"disabled_long_press_checked\":true,\"two_finger_taps_never_forward_clicks\":true,\"pointer_id_and_lift_order_checked\":true,\"hold_and_drag_boundaries_checked\":true,\"views_created\":false,\"android_runtime_executed\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n");
        System.out.println("PASS touch gestures: "+scenarios+" gesture traces, "+twoTaps+" two-finger taps, "+cancels+" releases and "+keyTraces+" Ctrl source combinations; no Views or game.");
    }
    static int key(TouchGestures.ControlKey key,int source,boolean down){return source==2?key.touch(down):key.physical(source==1,down);}
}
