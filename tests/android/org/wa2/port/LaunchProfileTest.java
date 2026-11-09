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
    static byte[] v2(int flags,String script,String font)throws Exception{byte[] a=script.getBytes("US-ASCII"),c=font.getBytes("US-ASCII"),b=new byte[16+a.length+c.length];b[0]='W';b[1]='A';b[2]='L';b[3]='S';b[4]=2;b[5]=(byte)flags;b[8]=(byte)a.length;b[10]=(byte)c.length;System.arraycopy(a,0,b,12,a.length);System.arraycopy(c,0,b,12+a.length,c.length);seal(b);return b;}
    public static void main(String[] args)throws Exception{
        int cases=0,guards=0;
        int languageCases=0,migrations=0,subtitleCases=0,v2Migrations=0;
        for(int flags=0;flags<16;++flags){LaunchProfile profile=new LaunchProfile(flags);check(profile.encode().length==20 && LaunchProfile.decode(profile.encode()).equals(profile));for(int flag:new int[]{1,2,4,8})check(profile.with(flag,true).enabled(flag) && !profile.with(flag,false).enabled(flag));++cases;}
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
        char[] longest=new char[124];Arrays.fill(longest,'a');String bound=new String(longest)+".pak";char[] uriTail=new char[4086];Arrays.fill(uriTail,'a');String maxUri="content://"+new String(uriTail);LaunchProfile maximum=new LaunchProfile(15,bound,bound,maxUri);check(maximum.encode().length==LaunchProfile.MAX_BYTES && LaunchProfile.decode(maximum.encode()).equals(maximum));++cases;
        try{new LaunchProfile(3,"a"+bound,"");throw new AssertionError();}catch(IllegalArgumentException expected){++guards;}
        for(int size:new int[]{0,1,15,17,4097}){byte[] bad=new byte[size];rejects(()->LaunchProfile.decode(bad));++guards;}
        rejects(()->LaunchProfile.decode(null));++guards;
        for(int at=0;at<16;++at){byte[] bad=LaunchProfile.defaults().encode();bad[at]^=(byte)128;rejects(()->LaunchProfile.decode(bad));++guards;}
        for(int at:new int[]{4,5,6,7,8,9,10,11,12,13,14,15}){byte[] bad=LaunchProfile.defaults().encode();bad[at]=(byte)255;seal(bad);rejects(()->LaunchProfile.decode(bad));++guards;}
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
        for(int flags=0;flags<16;++flags){LaunchProfile prior=LaunchProfile.decode(v2(flags,"script-en.pak","ic/font-en.pak"));check(prior.flags==flags && prior.script.equals("script-en.pak") && prior.font.equals("ic/font-en.pak") && prior.subtitle.isEmpty());++v2Migrations;++cases;}
        for(int flags=0;flags<16;++flags)for(String uri:new String[]{"","content://provider/document/opaque_123","content://provider/document/目录%2F字幕.yaml"}){LaunchProfile p=new LaunchProfile(flags,"script.pak","font.pak",uri);check(LaunchProfile.decode(p.encode()).equals(p));for(int flag:new int[]{1,2,4,8})check(p.with(flag,!p.enabled(flag)).subtitle.equals(uri));check(p.withArchive(true,"font-new.pak").subtitle.equals(uri) && p.withArchive(false,"").subtitle.equals(uri));++subtitleCases;++cases;}
        for(String bad:new String[]{"file:///sub.yaml","https://example.invalid/sub.yaml","relative/sub.yaml","content://","content://provider/a\n.yaml","content://provider/a\0.yaml","content://provider/\ud800",maxUri+"a"}){try{LaunchProfile.defaults().withSubtitle(bad);throw new AssertionError("Accepted invalid subtitle URI");}catch(IllegalArgumentException expected){++guards;}}
        store.setSubtitle("content://provider/document/a.yaml");first.duringWrite=()->{store.setSubtitle("content://provider/document/b.yaml");store.setArchive(true,"font-other.pak");};store.flush();check(store.dirty() && LaunchProfile.decode(first.bytes).subtitle.endsWith("a.yaml"));store.flush();store.reload();check(store.current().subtitle.endsWith("b.yaml") && store.current().font.equals("font-other.pak") && store.current().script.equals("script-en.pak") && independent.current().subtitle.isEmpty());++cases;
        first.failWrite=true;store.setSubtitle("");rejects(store::flush);check(store.dirty() && LaunchProfile.decode(first.bytes).subtitle.endsWith("b.yaml"));first.failWrite=false;store.flush();check(LaunchProfile.decode(first.bytes).subtitle.isEmpty());++guards;++cases;
        byte[] malformed=LaunchProfile.defaults().withSubtitle("content://provider/a").encode();malformed[malformed.length-5]=(byte)255;seal(malformed);rejects(()->LaunchProfile.decode(malformed));++guards;
        String report="{\"schema\":\"wa2.android-launch-profile.v3\",\"cases\":"+cases+",\"guards\":"+guards+",\"language_roundtrips\":"+languageCases+",\"legacy_migrations\":"+migrations+",\"v2_migrations\":"+v2Migrations+",\"external_subtitle_roundtrips\":"+subtitleCases+",\"external_subtitle_write_retry\":true,\"language_arguments_exact\":true,\"language_edits_preserve_flags\":true,\"directory_isolation\":true,\"unchanged_writes_suppressed\":true,\"failed_write_retry\":true,\"inflight_edit_not_lost\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
