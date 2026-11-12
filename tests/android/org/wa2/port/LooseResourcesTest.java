package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;

public final class LooseResourcesTest {
    static int cases,guards;
    static void check(boolean value){if(!value)throw new AssertionError();++cases;}
    interface Attempt{void run() throws IOException;}
    static void rejected(Attempt action) throws IOException {boolean failed=false;try{action.run();}catch(IOException error){failed=true;}check(failed);++guards;}
    public static void main(String[] args) throws Exception {
        for(String name:new String[]{"grp/a.tga","IC/char/b.tga","fon/本体80.tga","ck-gal/1001.txt","en/1001.bnr","font_ua/14pt本体.tga","grp/buf/a.tga","grp/nested/a.tga"})check(LooseResourcePath.parts(name,false).length>=2);
        check(LooseResourcePath.parts("",true).length==0);
        for(String path:new String[]{"","/grp/a.tga","grp//a.tga","grp/../a.tga","../grp/a.tga","grp/./a.tga","grp/a.tga/","grp\\a.tga","C:/grp/a.tga","grp/a:tga","grp/a\u0000.tga","grp/a\n.tga","grp/a?.tga","grp/a*.tga","grp/a.tga.","grp/a.tga ","a/b/c/d/e/f/g/h/i"})rejected(()->LooseResourcePath.parts(path,false));
        check(LooseResourcePath.fold("IC/GRP/A.TGA").equals("ic/grp/a.tga"));check(LooseResourcePath.fold("本体80.tga").equals("本体80.tga"));
        long[] now={0};int[] reads={0};HashMap<String,List<String>> source=new HashMap<>();
        source.put("root",Arrays.asList("GRP","IC"));source.put("grp",new ArrayList<>(Arrays.asList("A.TGA","b.tga")));
        ReadOnlyDirectoryIndex<String,String> index=new ReadOnlyDirectoryIndex<>(key->{++reads[0];List<String> result=source.get(key);return result==null?Collections.emptyList():result;},name->name,()->now[0]);
        for(int n=0;n<400;++n)check(index.at("root",true).find("grp").equals("GRP"));check(reads[0]==1);
        check(index.at("grp",false).find("a.tga").equals("A.TGA"));int initial=reads[0];
        source.get("grp").add("new.tga");now[0]=249999999L;check(index.at("grp",false).find("new.tga")==null && reads[0]==initial);
        now[0]=250000000L;check(index.at("grp",false).find("new.tga").equals("new.tga") && reads[0]==initial+1);
        now[0]+=250000000L;check(index.at("root",true).find("missing")==null && reads[0]==initial+1);
        // A second mount cannot reuse another authorized tree's names.
        ReadOnlyDirectoryIndex<String,String> other=new ReadOnlyDirectoryIndex<>(key->Arrays.asList("different.tga"),name->name,()->now[0]);check(other.at("grp",false).find("a.tga")==null);
        source.put("duplicate",Arrays.asList("same.tga","SAME.TGA","okay.tga"));check(index.at("duplicate",false).find("okay.tga").equals("okay.tga"));rejected(()->index.at("duplicate",false).find("same.tga"));
        for(int n=0;n<20;++n)index.at("folder-"+n,false);check(index.cachedFolders()==8);
        source.put("big",Collections.nCopies(40000,"a"));source.put("big2",Collections.nCopies(40000,"b"));index.at("big",false);index.at("big2",false);check(index.cachedEntries()<=65536);
        source.put("oversize",Collections.nCopies(65537,"x"));rejected(()->index.at("oversize",false));
        int[] attempts={0};ReadOnlyDirectoryIndex<String,String> retry=new ReadOnlyDirectoryIndex<>(key->{if(++attempts[0]==1)throw new IOException("transient");return Arrays.asList("fine");},name->name,()->now[0]);rejected(()->retry.at("grp",false));check(retry.at("grp",false).find("fine").equals("fine") && attempts[0]==2);
        index.clear();source.put("root",Arrays.asList("CHANGED"));check(index.at("root",true).find("changed").equals("CHANGED"));
        Files.write(Paths.get(args[0]),("{\"cases\":"+cases+",\"guards\":"+guards+",\"same_root_queries_for_400_lookups\":1,\"non_sliding_age_ms\":250,\"maximum_folders\":8,\"maximum_cached_entries\":65536,\"directory_isolation\":true,\"duplicate_name_and_size_guards\":true,\"failed_query_retry\":true,\"payloads_and_descriptors_cached\":false,\"android_device_verified\":false,\"passed\":true}\n").getBytes(StandardCharsets.UTF_8));
    }
}
