package org.wa2.port;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.charset.StandardCharsets;
public final class CompatibleStoragePathTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args)throws Exception{
        check(args.length==1);int cases=0,guards=0;
        String[] roots={"","Documents/Leaf/WHITE ALBUM2/","Documents/Leaf/WHITE ALBUM2 -closing chapter-/","Documents/Leaf/WHITE ALBUM2 -introductory chapter-/"};
        for(String profile:new String[]{"","test-mode/"})for(String root:roots){
            for(String name:new String[]{"Sys.sav","SYSTEM.ini","D3D.ini","default.ini","save_q.sav"}){String path=profile+root+name;check(CompatibleStoragePath.valid(path,false));check(CompatibleStoragePath.valid(path.toUpperCase(java.util.Locale.ROOT),false));check(CompatibleStoragePath.parts(path,false).length>=1);++cases;}
            for(int n=0;n<100;++n){String path=profile+root+String.format(java.util.Locale.ROOT,"save_%02d.sav",n);check(CompatibleStoragePath.valid(path,false));++cases;}
        }
        for(String profile:new String[]{"","test-mode/"})for(String folder:new String[]{"","Documents","Documents/Leaf","Documents/Leaf/WHITE ALBUM2","Documents/Leaf/WHITE ALBUM2 -closing chapter-","Documents/Leaf/WHITE ALBUM2 -introductory chapter-"}){
            String path=profile+folder;if(path.equals("test-mode/"))path="test-mode";check(CompatibleStoragePath.valid(path,true));++cases;
        }
        for(String path:new String[]{null,"../Sys.sav","/Sys.sav","C:/Sys.sav","Documents/../Sys.sav","Sys.sav\0","IC/Sys.sav","grp.pak","IC/grp.pak","native-settings.wa2cfg","native-saves/slot-000.wa2sav","test-mode/test-mode/Sys.sav","save_100.sav","save_-1.sav","save_０１.sav","save_01.sav/child","Documents/Leaf/Other/Sys.sav","Sys.sav.stage","Sys.sav.backup","*.sav","Documents\\Leaf\\WHITE ALBUM2\\Sys.sav"}){
            check(!CompatibleStoragePath.valid(path,false));++guards;
        }
        check(!CompatibleStoragePath.valid("test-mode//Sys.sav",false));++guards;
        String json="{\"schema\":\"wa2.compatible-storage-path.v1\",\"cases\":"+cases+",\"guards\":"+guards+",\"root_and_pc_chapter_paths\":true,\"test_mode_isolation\":true,\"original_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
        Files.write(Paths.get(args[0]),json.getBytes(StandardCharsets.UTF_8));
    }
}
