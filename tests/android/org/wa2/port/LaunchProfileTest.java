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
    static void seal(byte[] bytes){CRC32 crc=new CRC32();crc.update(bytes,0,bytes.length-4);long sum=crc.getValue();for(int n=0;n<4;++n)bytes[bytes.length-4+n]=(byte)(sum>>(n*8));}
    static byte[] oldProfile(int flags){byte[] b=new byte[16];b[0]='W';b[1]='A';b[2]='L';b[3]='S';b[4]=1;b[5]=(byte)flags;seal(b);return b;}
    public static void main(String[] args)throws Exception{
        int cases=0,guards=0;
        int languageCases=0,migrations=0;
        for(int flags=0;flags<16;++flags){LaunchProfile profile=new LaunchProfile(flags);check(profile.encode().length==16 && LaunchProfile.decode(profile.encode()).equals(profile));for(int flag:new int[]{1,2,4,8})check(profile.with(flag,true).enabled(flag) && !profile.with(flag,false).enabled(flag));++cases;}
        for(int flags=0;flags<8;++flags){LaunchProfile migrated=LaunchProfile.decode(oldProfile(flags));check(migrated.flags==(flags|8) && migrated.script.isEmpty() && migrated.font.isEmpty());check(LaunchProfile.decode(migrated.encode()).equals(migrated));++migrations;++cases;}
        String[] scripts={"","script-en.pak","IC/Script-JA.PAK"},fonts={"","font-en.pak","IC/FONT-JA.PAK"};
        for(int flags=0;flags<16;++flags)for(String script:scripts)for(String font:fonts){
            LaunchProfile p=new LaunchProfile(flags,script,font),decoded=LaunchProfile.decode(p.encode());check(decoded.equals(p) && p.hashCode()==decoded.hashCode());
            for(int flag:new int[]{1,2,4,8}){LaunchProfile changed=p.with(flag,!p.enabled(flag));check(changed.script.equals(p.script) && changed.font.equals(p.font));}
            java.util.List<String> argsList=Arrays.asList(p.gameArguments());check(argsList.contains("--test-mode")==p.enabled(4));check(argsList.contains("--no-chinese-subtitles")==!p.enabled(8));
            check(argsList.contains("--script-pak")==!script.isEmpty() && argsList.contains("--font-pak")==!font.isEmpty());
            if(!script.isEmpty())check(argsList.get(argsList.indexOf("--script-pak")+1).equals(p.script));if(!font.isEmpty())check(argsList.get(argsList.indexOf("--font-pak")+1).equals(p.font));++languageCases;++cases;
        }
        check(LaunchProfile.defaults().gameArguments().length==0);
        for(String bad:new String[]{"../bad.pak","other/a.pak","IC/../bad.pak","IC\\font.pak",".hidden.pak","font.ttf","font.otf","bad/name.pak","x\0.pak","字体.pak","\u212A.pak"}){try{new LaunchProfile(3,bad,"");throw new AssertionError("Accepted invalid PAK: "+bad);}catch(IllegalArgumentException expected){++guards;}}
        char[] longest=new char[124];Arrays.fill(longest,'a');String bound=new String(longest)+".pak";LaunchProfile maximum=new LaunchProfile(15,bound,bound);check(maximum.encode().length==LaunchProfile.MAX_BYTES && LaunchProfile.decode(maximum.encode()).equals(maximum));++cases;
        try{new LaunchProfile(3,"a"+bound,"");throw new AssertionError();}catch(IllegalArgumentException expected){++guards;}
        for(int size:new int[]{0,1,15,17,4097}){byte[] bad=new byte[size];rejects(()->LaunchProfile.decode(bad));++guards;}
        rejects(()->LaunchProfile.decode(null));++guards;
        for(int at=0;at<16;++at){byte[] bad=LaunchProfile.defaults().encode();bad[at]^=(byte)128;rejects(()->LaunchProfile.decode(bad));++guards;}
        for(int at:new int[]{4,5,6,7,8,9,10,11}){byte[] bad=LaunchProfile.defaults().encode();bad[at]=(byte)255;seal(bad);rejects(()->LaunchProfile.decode(bad));++guards;}
        for(int at:new int[]{6,7,8,9,10,11,12}){byte[] bad=new LaunchProfile(11,"a.pak","b.pak").encode();bad[at]=(byte)255;seal(bad);rejects(()->LaunchProfile.decode(bad));++guards;}
        Storage first=new Storage();LaunchProfileStore store=new LaunchProfileStore(first,LaunchProfile.defaults());check(store.current().flags==11 && store.dirty() && first.writes==0);store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==11 && first.writes==1);++cases;
        store.flush();check(!store.set(LaunchProfile.FPS,true) && first.writes==1);++cases;
        check(store.set(LaunchProfile.FPS,false) && store.dirty() && store.current().flags==10);store.flush();check(first.writes==2 && LaunchProfile.decode(first.bytes).flags==10 && !store.dirty());++cases;
        first.failWrite=true;store.set(LaunchProfile.TEST_MODE,true);rejects(store::flush);check(store.dirty() && store.current().flags==14 && LaunchProfile.decode(first.bytes).flags==10);first.failWrite=false;store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==14);++guards;++cases;
        first.duringWrite=()->store.set(LaunchProfile.LONG_PRESS,false);store.set(LaunchProfile.FPS,true);store.flush();check(store.dirty() && store.current().flags==13 && LaunchProfile.decode(first.bytes).flags==15);store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).flags==13);++cases;
        store.set(LaunchProfile.TEST_MODE,false);rejects(store::reload);store.flush();++guards;
        first.failRead=true;rejects(store::reload);check(store.current().flags==9 && !store.dirty());first.failRead=false;++guards;
        byte[] before=first.bytes.clone();first.bytes[15]^=1;rejects(store::reload);check(store.current().flags==9 && !store.dirty() && !Arrays.equals(first.bytes,before));first.bytes=before;++guards;
        first.bytes=new LaunchProfile(0).encode();store.reload();check(store.current().flags==0 && !store.dirty());++cases;
        first.bytes=null;store.reload();check(store.current().flags==11 && store.dirty());store.flush();check(LaunchProfile.decode(first.bytes).flags==11);++cases;
        Storage second=new Storage();second.bytes=new LaunchProfile(4).encode();LaunchProfileStore independent=new LaunchProfileStore(second,new LaunchProfile(7));check(independent.current().flags==4 && store.current().flags==11);independent.set(LaunchProfile.FPS,true);independent.flush();check(LaunchProfile.decode(second.bytes).flags==5 && LaunchProfile.decode(first.bytes).flags==11);++cases;
        Storage legacy=new Storage();legacy.bytes=oldProfile(6);LaunchProfileStore migrated=new LaunchProfileStore(legacy,LaunchProfile.defaults());check(migrated.current().flags==14 && migrated.current().script.isEmpty() && !migrated.dirty());migrated.setArchive(false,"SCRIPT-EN.PAK");migrated.flush();LaunchProfileStore reopened=new LaunchProfileStore(legacy,LaunchProfile.defaults());check(reopened.current().flags==14 && reopened.current().script.equals("script-en.pak") && !reopened.dirty());++cases;
        check(store.setArchive(false,"script-ja.pak"));first.duringWrite=()->{store.setArchive(true,"font-ja.pak");store.set(8,false);};store.flush();check(store.dirty() && LaunchProfile.decode(first.bytes).font.isEmpty() && store.current().font.equals("font-ja.pak"));store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).equals(store.current()));
        check(store.current().script.equals("script-ja.pak") && !store.current().enabled(8) && independent.current().script.isEmpty() && independent.current().font.isEmpty());++cases;
        int writes=first.writes;check(!store.setArchive(false,"SCRIPT-JA.PAK"));store.flush();check(first.writes==writes);++cases;
        first.failWrite=true;store.setArchive(false,"script-en.pak");rejects(store::flush);check(store.dirty() && store.current().font.equals("font-ja.pak") && LaunchProfile.decode(first.bytes).script.equals("script-ja.pak"));first.failWrite=false;store.flush();store.reload();check(store.current().script.equals("script-en.pak") && store.current().font.equals("font-ja.pak"));++guards;++cases;
        String report="{\"schema\":\"wa2.android-launch-profile.v2\",\"cases\":"+cases+",\"guards\":"+guards+",\"language_roundtrips\":"+languageCases+",\"legacy_migrations\":"+migrations+",\"language_arguments_exact\":true,\"language_edits_preserve_flags\":true,\"directory_isolation\":true,\"unchanged_writes_suppressed\":true,\"failed_write_retry\":true,\"inflight_edit_not_lost\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
