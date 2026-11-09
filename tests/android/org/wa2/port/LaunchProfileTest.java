package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class LaunchProfileTest {
    static int cases,guards;
    static void check(boolean value){if(!value)throw new AssertionError();}
    interface Attempt{void run()throws Exception;}
    static void rejects(Attempt action)throws Exception{try{action.run();}catch(IOException | IllegalArgumentException expected){++guards;return;}throw new AssertionError("Expected failure");}
    static final class Storage implements LaunchProfileStore.Storage {
        byte[] bytes;int reads,writes;boolean failRead,failWrite;Runnable duringWrite;
        public byte[] read()throws IOException{++reads;if(failRead)throw new IOException("injected read failure");return bytes==null?null:bytes.clone();}
        public void replace(byte[] value)throws IOException{if(failWrite)throw new IOException("injected write failure");++writes;bytes=value.clone();if(duringWrite!=null){Runnable action=duringWrite;duringWrite=null;action.run();}}
    }
    static void seal(byte[] bytes){CRC32 crc=new CRC32();crc.update(bytes,0,bytes.length-4);long sum=crc.getValue();for(int n=0;n<4;++n)bytes[bytes.length-4+n]=(byte)(sum>>(n*8));}
    static void word(byte[] bytes,int at,int value){bytes[at]=(byte)value;bytes[at+1]=(byte)(value>>8);}
    static byte[] legacy(int version,int flags,String script,String font,String subtitle){
        byte[] a=script.getBytes(StandardCharsets.UTF_8),b=font.getBytes(StandardCharsets.UTF_8),c=subtitle.getBytes(StandardCharsets.UTF_8);
        int offset=version==3?16:12;byte[] data=new byte[offset+4+a.length+b.length+c.length];
        data[0]='W';data[1]='A';data[2]='L';data[3]='S';data[4]=(byte)version;data[5]=(byte)flags;
        if(version!=1){word(data,8,a.length);word(data,10,b.length);if(version==3)word(data,12,c.length);}
        System.arraycopy(a,0,data,offset,a.length);System.arraycopy(b,0,data,offset+a.length,b.length);System.arraycopy(c,0,data,offset+a.length+b.length,c.length);seal(data);return data;
    }
    static void paired(LaunchProfile p,int expected){
        check(p.language==expected);
        check(p.languageTag().equals(expected==1?"ja-JP":"zh-CN"));
        check(p.scriptArchive().equals(expected==1?"script.pak":"ck-gal.pak"));
        check(p.fontArchive().equals(expected==1?"fnt.pak":"fon.pak"));
    }
    public static void main(String[] args)throws Exception{
        int languages=0,v1=0,v2=0,v3=0,v4=0,subtitles=0;
        String[] uris={"","content://provider/document/opaque_123","content://provider/document/目录%2F字幕.yaml"};
        for(int flags=0;flags<16;++flags)for(int language=0;language<2;++language){
            LaunchProfile p=new LaunchProfile(flags,language,"");paired(p,language);check(p.encode().length==16 && p.encode()[4]==5 && !LaunchProfile.needsUpgrade(p.encode()));
            LaunchProfile decoded=LaunchProfile.decode(p.encode());check(decoded.equals(p) && p.hashCode()==decoded.hashCode());
            for(int flag:new int[]{1,2,4,8}){LaunchProfile changed=p.with(flag,!p.enabled(flag));paired(changed,language);check(changed.enabled(flag)!=p.enabled(flag));}
            java.util.ArrayList<String> expected=new java.util.ArrayList<>();if(p.enabled(4))expected.add("--test-mode");if(!p.enabled(8))expected.add("--no-chinese-subtitles");expected.add("--language");expected.add(language==1?"ja-JP":"zh-CN");
            check(Arrays.asList(p.gameArguments()).equals(expected));++languages;++cases;
        }
        paired(LaunchProfile.defaults(),0);check(LaunchProfile.defaults().flags==11);
        for(int flags=0;flags<16;++flags)for(int language=0;language<2;++language)for(String uri:uris){
            LaunchProfile p=new LaunchProfile(flags,language,uri);check(LaunchProfile.decode(p.encode()).equals(p));
            for(int flag:new int[]{1,2,4,8})check(p.with(flag,!p.enabled(flag)).subtitle.equals(uri));
            LaunchProfile changed=p.withLanguage(1-language);check(changed.flags==flags && changed.subtitle.equals(uri));paired(changed,1-language);++subtitles;++cases;
        }
        for(int flags=0;flags<8;++flags){byte[] old=legacy(1,flags,"","","");LaunchProfile p=LaunchProfile.decode(old);check(p.flags==(flags|8) && p.subtitle.isEmpty() && LaunchProfile.needsUpgrade(old));paired(p,0);++v1;++cases;}
        String[][] pairs={{"",""},{"script.pak","fon.pak"},{"CK-GAL.PAK","fnt.pak"},{"","IC/FNT.PAK"},{"custom-script.pak","font-en.pak"},{"IC/SCRIPT.PAK","IC/FON.PAK"}};
        for(int flags=0;flags<16;++flags)for(String[] pair:pairs)for(int version:new int[]{2,3})for(String uri:version==2?new String[]{""}:uris){
            byte[] old=legacy(version,flags,pair[0],pair[1],uri);LaunchProfile p=LaunchProfile.decode(old);paired(p,0);check(p.flags==flags && p.subtitle.equals(uri) && LaunchProfile.needsUpgrade(old));
            Storage files=new Storage();files.bytes=old;LaunchProfileStore migrated=new LaunchProfileStore(files,LaunchProfile.defaults());check(migrated.dirty() && migrated.current().equals(p));migrated.flush();check(files.writes==1 && !migrated.dirty() && files.bytes[4]==5 && LaunchProfile.decode(files.bytes).equals(p));
            if(version==2)++v2;else ++v3;++cases;
        }
        for(int flags=0;flags<16;++flags)for(int language=0;language<2;++language)for(String uri:uris){
            byte[] old=new LaunchProfile(flags,language,uri).encode();old[4]=4;seal(old);
            Storage files=new Storage();files.bytes=old;LaunchProfileStore migrated=new LaunchProfileStore(files,LaunchProfile.defaults());
            paired(migrated.current(),0);check(migrated.dirty() && migrated.current().flags==flags && migrated.current().subtitle.equals(uri));
            migrated.flush();check(files.bytes[4]==5 && !migrated.dirty());
            migrated.setLanguage(1);migrated.flush();migrated.reload();paired(migrated.current(),1);check(!migrated.dirty() && migrated.current().flags==flags && migrated.current().subtitle.equals(uri));++v4;++cases;
        }
        {byte[] old=new LaunchProfile(15,1,uris[1]).encode();old[4]=4;old[6]=2;seal(old);rejects(()->LaunchProfile.decode(old));}
        char[] tail=new char[4086];Arrays.fill(tail,'a');String maxUri="content://"+new String(tail);
        LaunchProfile max=new LaunchProfile(15,1,maxUri);check(max.encode().length==4112 && LaunchProfile.decode(max.encode()).equals(max));++cases;
        char[] leaf=new char[124];Arrays.fill(leaf,'a');String maxPak=new String(leaf)+".pak";byte[] maximumLegacy=legacy(3,15,maxPak,maxPak,maxUri);check(maximumLegacy.length==LaunchProfile.MAX_BYTES);check(LaunchProfile.decode(maximumLegacy).subtitle.equals(maxUri));++cases;
        for(int size:new int[]{0,1,15,17,LaunchProfile.MAX_BYTES+1}){byte[] bad=new byte[size];rejects(()->LaunchProfile.decode(bad));}
        rejects(()->LaunchProfile.decode(null));
        for(int at=0;at<16;++at){byte[] bad=LaunchProfile.defaults().encode();bad[at]^=(byte)128;rejects(()->LaunchProfile.decode(bad));}
        for(int at:new int[]{0,1,2,3,4,5,6,7,8,9,10,11}){byte[] bad=LaunchProfile.defaults().encode();bad[at]=(byte)255;seal(bad);rejects(()->LaunchProfile.decode(bad));}
        for(int invalid:new int[]{-1,2,255})rejects(()->LaunchProfile.defaults().withLanguage(invalid));
        rejects(()->new LaunchProfile(16));rejects(()->LaunchProfile.defaults().with(16,true));
        for(String bad:new String[]{"file:///sub.yaml","https://example.invalid/sub.yaml","relative/sub.yaml","content://","content://provider/a\n.yaml","content://provider/a\0.yaml","content://provider/\ud800",maxUri+"a"})rejects(()->LaunchProfile.defaults().withSubtitle(bad));
        for(String bad:new String[]{"../bad.pak","other/a.pak","IC/../bad.pak","IC\\font.pak",".hidden.pak","font.ttf","字体.pak","a"+maxPak})rejects(()->LaunchProfile.decode(legacy(3,15,bad,"", "")));
        byte[] malformed=max.withSubtitle("content://provider/a").encode();malformed[malformed.length-5]=(byte)255;seal(malformed);rejects(()->LaunchProfile.decode(malformed));
        for(int version:new int[]{1,2,3}){byte[] old=legacy(version,7,"","","");old[6]=1;seal(old);rejects(()->LaunchProfile.decode(old));}
        Storage first=new Storage();LaunchProfileStore store=new LaunchProfileStore(first,LaunchProfile.defaults());check(store.current().flags==11 && store.dirty() && first.writes==0);store.flush();check(!store.dirty() && first.writes==1);++cases;
        int writes=first.writes;check(!store.set(1,true) && !store.setLanguage(0) && !store.setSubtitle(""));store.flush();check(first.writes==writes);++cases;
        store.setLanguage(1);first.failWrite=true;rejects(store::flush);paired(store.current(),1);paired(LaunchProfile.decode(first.bytes),0);check(store.dirty());first.failWrite=false;store.flush();store.reload();paired(store.current(),1);check(!store.dirty());++cases;
        first.duringWrite=()->{store.setLanguage(0);store.set(8,false);store.setSubtitle(uris[1]);};store.set(4,true);store.flush();check(store.dirty());paired(LaunchProfile.decode(first.bytes),1);check(LaunchProfile.decode(first.bytes).flags==15);store.flush();store.reload();paired(store.current(),0);check(!store.current().enabled(8) && store.current().enabled(4) && store.current().subtitle.equals(uris[1]));++cases;
        store.set(1,false);rejects(store::reload);store.flush();LaunchProfile saved=store.current();first.failRead=true;rejects(store::reload);check(store.current().equals(saved) && !store.dirty());first.failRead=false;
        byte[] before=first.bytes.clone();first.bytes[15]^=1;rejects(store::reload);check(store.current().equals(saved) && !store.dirty());first.bytes=before;++cases;
        Storage second=new Storage();second.bytes=new LaunchProfile(4,1,"").encode();LaunchProfileStore independent=new LaunchProfileStore(second,LaunchProfile.defaults());check(independent.current().flags==4);paired(independent.current(),1);independent.set(1,true);independent.flush();check(LaunchProfile.decode(first.bytes).equals(saved) && LaunchProfile.decode(second.bytes).flags==5);++cases;
        first.bytes=null;store.reload();check(store.current().equals(LaunchProfile.defaults()) && store.dirty());store.flush();++cases;
        first.bytes=legacy(3,10,"script.pak","fon.pak",uris[2]);store.reload();check(store.dirty());paired(store.current(),0);first.failWrite=true;rejects(store::flush);check(store.dirty() && first.bytes[4]==3);first.failWrite=false;store.flush();check(first.bytes[4]==5 && LaunchProfile.decode(first.bytes).subtitle.equals(uris[2]) && store.current().flags==10);++cases;
        first.failWrite=true;store.setSubtitle(uris[1]);rejects(store::flush);check(store.dirty() && LaunchProfile.decode(first.bytes).subtitle.equals(uris[2]));first.failWrite=false;store.flush();store.reload();check(store.current().subtitle.equals(uris[1]));++cases;
        first.duringWrite=()->{store.setSubtitle("");store.setLanguage(0);};store.setSubtitle(uris[2]);store.flush();check(store.dirty() && LaunchProfile.decode(first.bytes).subtitle.equals(uris[2]));store.flush();check(!store.dirty() && LaunchProfile.decode(first.bytes).subtitle.isEmpty());paired(store.current(),0);check(independent.current().subtitle.isEmpty());++cases;
        String report="{\"schema\":\"wa2.android-launch-profile.v5\",\"cases\":"+cases+",\"guards\":"+guards+",\"language_roundtrips\":"+languages+",\"legacy_migrations\":"+v1+",\"v2_migrations\":"+v2+",\"v3_migrations\":"+v3+",\"v4_migrations\":"+v4+",\"external_subtitle_roundtrips\":"+subtitles+",\"automatic_matched_pairs\":true,\"legacy_mixed_pair_corrected\":true,\"older_profiles_default_chinese\":true,\"explicit_language_retained_after_upgrade\":true,\"upgrade_write_retry\":true,\"external_subtitle_write_retry\":true,\"language_arguments_exact\":true,\"language_edits_preserve_flags\":true,\"directory_isolation\":true,\"unchanged_writes_suppressed\":true,\"failed_write_retry\":true,\"inflight_edit_not_lost\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
        Files.write(Paths.get(args[0]),report.getBytes(StandardCharsets.UTF_8));
    }
}
