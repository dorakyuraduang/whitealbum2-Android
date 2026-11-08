package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class LaunchProfileTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    interface Attempt{void run()throws IOException;}
    static void rejects(Attempt action)throws IOException{try{action.run();}catch(IOException expected){return;}throw new AssertionError("Expected failure");}
    static final class Storage implements LaunchProfileStore.Storage {
        byte[] bytes;int reads,writes;boolean failRead,failWrite;Runnable duringWrite;
        public byte[] read()throws IOException{++reads;if(failRead)throw new IOException("injected read failure");return bytes==null?null:bytes.clone();}
        public void replace(byte[] value)throws IOException{if(failWrite)throw new IOException("injected write failure");++writes;bytes=value.clone();if(duringWrite!=null){Runnable action=duringWrite;duringWrite=null;action.run();}}
    }
    static void seal(byte[] bytes){CRC32 crc=new CRC32();crc.update(bytes,0,12);long sum=crc.getValue();for(int n=0;n<4;++n)bytes[12+n]=(byte)(sum>>(n*8));}
    public static void main(String[] args)throws Exception{
        int cases=0,guards=0;
        for(int flags=0;flags<8;++flags){LaunchProfile profile=new LaunchProfile(flags);check(profile.encode().length==16 && LaunchProfile.decode(profile.encode()).flags==flags);for(int flag:new int[]{1,2,4})check(profile.with(flag,true).enabled(flag) && !profile.with(flag,false).enabled(flag));++cases;}
        for(int size:new int[]{0,1,15,17,4097}){byte[] bad=new byte[size];rejects(()->LaunchProfile.decode(bad));++guards;}
        rejects(()->LaunchProfile.decode(null));++guards;
        for(int at=0;at<16;++at){byte[] bad=LaunchProfile.defaults().encode();bad[at]^=(byte)128;rejects(()->LaunchProfile.decode(bad));++guards;}
        for(int at:new int[]{4,5,6,7,8,9,10,11}){byte[] bad=LaunchProfile.defaults().encode();bad[at]=(byte)255;seal(bad);rejects(()->LaunchProfile.decode(bad));++guards;}
        Storage first=new Storage();LaunchProfileStore store=new LaunchProfileStore(first,LaunchProfile.defaults());check(store.current().flags==3 && store.dirty() && first.writes==0);store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==3 && first.writes==1);++cases;
        store.flush();check(!store.set(LaunchProfile.FPS,true) && first.writes==1);++cases;
        check(store.set(LaunchProfile.FPS,false) && store.dirty() && store.current().flags==2);store.flush();check(first.writes==2 && LaunchProfile.decode(first.bytes).flags==2 && !store.dirty());++cases;
        first.failWrite=true;store.set(LaunchProfile.TEST_MODE,true);rejects(store::flush);check(store.dirty() && store.current().flags==6 && LaunchProfile.decode(first.bytes).flags==2);first.failWrite=false;store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==6);++guards;++cases;
        first.duringWrite=()->store.set(LaunchProfile.LONG_PRESS,false);store.set(LaunchProfile.FPS,true);store.flush();check(store.dirty() && store.current().flags==5 && LaunchProfile.decode(first.bytes).flags==7);store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==5);++cases;
        store.set(LaunchProfile.TEST_MODE,false);rejects(store::reload);store.flush();++guards;
        first.failRead=true;rejects(store::reload);check(store.current().flags==1 && !store.dirty());first.failRead=false;++guards;
        byte[] before=first.bytes.clone();first.bytes[15]^=1;rejects(store::reload);check(store.current().flags==1 && !store.dirty() && !Arrays.equals(first.bytes,before));first.bytes=before;++guards;
        first.bytes=new LaunchProfile(0).encode();store.reload();check(store.current().flags==0 && !store.dirty());++cases;
        first.bytes=null;store.reload();check(store.current().flags==3 && store.dirty());store.flush();check(LaunchProfile.decode(first.bytes).flags==3);++cases;
        Storage second=new Storage();second.bytes=new LaunchProfile(4).encode();LaunchProfileStore independent=new LaunchProfileStore(second,new LaunchProfile(7));check(independent.current().flags==4 && store.current().flags==3);independent.set(LaunchProfile.FPS,true);independent.flush();check(LaunchProfile.decode(second.bytes).flags==5 && LaunchProfile.decode(first.bytes).flags==3);++cases;
        Storage legacy=new Storage();LaunchProfileStore migrated=new LaunchProfileStore(legacy,new LaunchProfile(6));migrated.flush();check(LaunchProfile.decode(legacy.bytes).flags==6);LaunchProfileStore reopened=new LaunchProfileStore(legacy,LaunchProfile.defaults());check(reopened.current().flags==6 && !reopened.dirty());++cases;
        String report="{\"schema\":\"wa2.android-launch-profile.v1\",\"cases\":"+cases+",\"guards\":"+guards+",\"directory_isolation\":true,\"unchanged_writes_suppressed\":true,\"failed_write_retry\":true,\"inflight_edit_not_lost\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
