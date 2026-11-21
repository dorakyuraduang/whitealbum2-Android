package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public final class LaunchTargetTest {
    private static int cases,guards;
    static void check(boolean value){if(!value)throw new AssertionError();}
    interface Attempt{void run()throws Exception;}
    static void rejects(Attempt attempt)throws Exception{try{attempt.run();}catch(IOException | IllegalArgumentException expected){++guards;return;}throw new AssertionError("Expected rejection");}
    static final class FilesInFolder {
        final Map<String,byte[]> bytes=new HashMap<>();boolean fail;
        LaunchProfileStore.Storage storage(LaunchTarget target){return new LaunchProfileStore.Storage(){
            public byte[] read(){byte[] data=bytes.get(target.file);return data==null?null:data.clone();}
            public void replace(byte[] data)throws IOException{if(fail)throw new IOException("Write failed");bytes.put(target.file,data.clone());}
        };}
    }
    public static void main(String[] args)throws Exception{
        check(LaunchTarget.MAIN.file.equals("native-launcher.wa2cfg"));
        for(LaunchTarget target:LaunchTarget.values()){
            check(GameStoragePath.valid(target.file,true) && GameStoragePath.valid(target.file,false));
            check(!GameStoragePath.valid(target.file+"/child",true));
            check(!GameStoragePath.valid("../"+target.file,true));
            check(target.storageKey(null)==null);++cases;
        }
        String tree="content://provider/tree/same";
        check(!LaunchTarget.MAIN.storageKey(tree).equals(LaunchTarget.SPECIAL.storageKey(tree)));
        for(int flags=0;flags<64;++flags)for(int language=0;language<2;++language){
            LaunchProfile profile=new LaunchProfile(flags,language,"");
            if((flags&LaunchProfile.TEST_MODE)!=0){rejects(()->LaunchTarget.SPECIAL.arguments(profile));continue;}
            LaunchProfile roundtrip=LaunchProfile.decode(profile.encode());LaunchTarget.SPECIAL.validate(roundtrip);
            java.util.ArrayList<String> expected=new java.util.ArrayList<>();if((flags&8)==0)expected.add("--no-chinese-subtitles");if((flags&32)!=0)expected.add("--limit-60-fps");expected.add("--language");expected.add(profile.languageTag());
            check(Arrays.equals(LaunchTarget.SPECIAL.arguments(roundtrip),expected.toArray(new String[0])));
            if(language==1)check(LaunchTarget.SPECIAL.ui(profile).equals("grp.pak") && LaunchTarget.SPECIAL.script(profile).equals("script.pak") && profile.fontArchive().equals("fnt.pak"));
            else check(LaunchTarget.SPECIAL.ui(profile).equals("gcn.pak") && LaunchTarget.SPECIAL.script(profile).equals("script_cn.pak / script_cn2.pak"));
            ++cases;
        }
        for(int language:new int[]{2,3})rejects(()->LaunchTarget.SPECIAL.arguments(new LaunchProfile(3,language,"")));
        LaunchTarget.SPECIAL.validate(new LaunchProfile(3,0,"content://provider/sub.yaml"));
        for(int language=0;language<4;++language){LaunchProfile p=new LaunchProfile(63,language,"content://provider/sub.yaml");check(Arrays.equals(LaunchTarget.MAIN.arguments(p),p.gameArguments()));++cases;}
        FilesInFolder files=new FilesInFolder();
        LaunchProfileStore main=new LaunchProfileStore(files.storage(LaunchTarget.MAIN),LaunchTarget.MAIN.defaults());
        LaunchProfileStore special=new LaunchProfileStore(files.storage(LaunchTarget.SPECIAL),LaunchTarget.SPECIAL.defaults());
        main.flush();special.flush();byte[] original=files.bytes.get(LaunchTarget.MAIN.file).clone();
        special.setLanguage(1);special.set(32,true);special.set(16,true);files.fail=true;rejects(special::flush);
        check(special.dirty() && Arrays.equals(original,files.bytes.get(LaunchTarget.MAIN.file)));files.fail=false;special.flush();special.reload();main.reload();
        check(special.current().language==1 && special.current().enabled(32) && special.current().enabled(16) && main.current().equals(LaunchTarget.MAIN.defaults()));++cases;
        main.setLanguage(2);main.setSubtitle("content://provider/sub.yaml");main.flush();special.reload();check(special.current().language==1 && special.current().subtitle.isEmpty());++cases;
        special.setSubtitle("content://provider/sc.yaml");special.set(8,true);special.flush();special.reload();
        check(special.current().subtitle.equals("content://provider/sc.yaml") && special.current().enabled(8) && !Arrays.asList(LaunchTarget.SPECIAL.arguments(special.current())).contains("--no-chinese-subtitles"));
        check(main.current().subtitle.equals("content://provider/sub.yaml"));++cases;
        special.set(8,false);special.flush();special.reload();check(!special.current().enabled(8) && Arrays.asList(LaunchTarget.SPECIAL.arguments(special.current())).contains("--no-chinese-subtitles"));check(special.current().subtitle.equals("content://provider/sc.yaml") && special.current().enabled(32));++cases;
        files.bytes.remove(LaunchTarget.SPECIAL.file);special.reload();check(special.current().equals(LaunchTarget.SPECIAL.defaults()) && special.dirty());special.flush();check(main.current().language==2);++cases;
        Files.write(Paths.get(args[0]),("{\"passed\":true,\"cases\":"+cases+",\"guards\":"+guards+",\"sc_subtitle_toggle_and_source_persisted\":true,\"same_folder_title_isolation\":true,\"failed_write_retry\":true,\"special_defaults_restored\":true,\"japanese_ui_script_font\":true,\"unsupported_special_options_rejected\":true,\"source_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"android_device_verified\":false}\n").getBytes(StandardCharsets.UTF_8));
    }
}
