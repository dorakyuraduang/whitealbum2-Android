package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

public final class DocumentDirectoryTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    interface Attempt {void run() throws IOException;}
    static void rejects(Attempt action)throws IOException{try{action.run();}catch(IOException expected){return;}throw new AssertionError("Expected failure");}
    static final class Entry {final String name;final long size;Entry(String name,long size){this.name=name;this.size=size;}}
    static final class Provider {
        final HashMap<String,List<Entry>> directories=new HashMap<>();int queries;
        List<Entry> children(String name)throws IOException{++queries;List<Entry> items=directories.get(name);if(items==null)throw new IOException("Missing directory");return new ArrayList<>(items);}
        DocumentDirectory<String,Entry> operation(){return new DocumentDirectory<>(this::children,item->item.name);}
    }
    public static void main(String[] args)throws Exception{
        Provider provider=new Provider();List<Entry> root=new ArrayList<>();provider.directories.put("root",root);
        for(int n=0;n<120;++n)root.add(new Entry("resource-"+n+".pak",100000));
        for(int n=0;n<100;++n)root.add(new Entry(String.format(java.util.Locale.ROOT,"save_%02d.sav",n),n+80));
        root.add(new Entry("Documents",0));int cases=0,lookups=0;
        DocumentDirectory<String,Entry> operation=provider.operation();
        for(int repeat=0;repeat<4;++repeat)for(int n=0;n<100;++n){String name=String.format(java.util.Locale.ROOT,"SAVE_%02d.SAV",n);check(operation.at("root").find(name).size==n+80);++lookups;}
        check(provider.queries==1 && operation.at("root").entries().size()==221);++cases;
        List<Entry> copied=operation.at("root").entries();copied.clear();check(operation.at("root").find("save_00.sav")!=null);++cases;
        // Publication/recovery updates the same operation's snapshot without
        // rescanning the directory or retaining another provider transaction.
        operation.at("root").put(new Entry("Sys.sav.wa2-old",7));
        Entry old=operation.at("root").find("sys.sav.wa2-old");check(old.size==7);
        operation.at("root").remove(old.name);operation.at("root").put(new Entry("Sys.sav",old.size));
        check(operation.at("root").find("sys.sav").size==7 && operation.at("root").find(old.name)==null && provider.queries==1);++cases;
        // A new storage API operation always reloads the provider. Changes
        // from the PC/file manager are visible immediately, without a TTL.
        root.add(new Entry("Sys.sav",99));operation.clear();
        check(operation.at("root").find("sys.sav").size==99 && provider.queries==2);++cases;
        root.remove(root.size()-1);check(provider.operation().at("root").find("sys.sav")==null && provider.queries==3);++cases;
        provider.directories.put("chapter",new ArrayList<>());check(operation.at("chapter").find("Sys.sav")==null && provider.queries==4);++cases;
        check(operation.at("chapter").find("SYS.SAV")==null && provider.queries==4);++cases;
        List<Entry> duplicate=new ArrayList<>();duplicate.add(new Entry("Sys.sav",1));duplicate.add(new Entry("SYS.SAV",2));provider.directories.put("duplicate",duplicate);
        DocumentDirectory<String,Entry> ambiguity=provider.operation();rejects(()->ambiguity.at("duplicate").find("Sys.sav"));++cases;
        rejects(()->ambiguity.at("missing"));++cases;
        List<Entry> excessive=new ArrayList<>();for(int n=0;n<10001;++n)excessive.add(new Entry("file"+n,0));provider.directories.put("excessive",excessive);
        rejects(()->provider.operation().at("excessive"));++cases;
        String report="{\"schema\":\"wa2.document-directory.v1\",\"cases\":"+cases+",\"lookups\":"+lookups+",\"same_directory_queries_for_400_lookups\":1,\"operation_scoped_snapshot\":true,\"next_operation_sees_external_edits\":true,\"publication_index_updates\":true,\"ambiguity_and_size_guards\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
        Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
