package org.wa2.port;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.function.Function;

/** Full metadata belongs to one operation. A save-list inventory can retain
 * the lighter ancestor snapshots while visiting its sibling banks. */
final class DocumentDirectory<K,T> {
    interface Reader<K,T> { List<T> children(K parent) throws IOException; }
    interface Name<T> { String name(T item); }
    final class Snapshot {
        private final LinkedHashMap<String,T> entries=new LinkedHashMap<>();
        private final HashSet<String> ambiguous=new HashSet<>();
        Snapshot(List<T> source) throws IOException {
            if(source.size()>10000)throw new IOException("Game directory has too many entries");
            for(T item:source){String key=key(names.name(item));if(entries.put(key,item)!=null)ambiguous.add(key);}
        }
        T find(String name) throws IOException {
            String key=key(name);if(ambiguous.contains(key))throw new IOException("Ambiguous game storage path: "+name);
            return entries.get(key);
        }
        List<T> entries(){return new ArrayList<>(entries.values());}
        void put(T item){entries.put(key(names.name(item)),item);}
        void remove(String name){entries.remove(key(name));ambiguous.remove(key(name));}
    }
    private final Reader<K,T> reader;
    private final Reader<K,T> pathReader;
    private final Name<T> names;
    private final HashMap<K,Snapshot> snapshots=new HashMap<>();
    private final HashMap<K,Snapshot> pathSnapshots=new HashMap<>();
    DocumentDirectory(Reader<K,T> reader,Name<T> names){this(reader,reader,names);}
    DocumentDirectory(Reader<K,T> reader,Reader<K,T> pathReader,Name<T> names){this.reader=reader;this.pathReader=pathReader;this.names=names;}
    void clear(){snapshots.clear();pathSnapshots.clear();}
    void clearFull(){snapshots.clear();}
    K resolve(K root,String[] parts,int count,Function<T,K> directory) throws IOException {
        K parent=root;
        for(int n=0;n<count;++n){
            T child=path(parent).find(parts[n]);
            if(child==null || (parent=directory.apply(child))==null)return null;
        }
        return parent;
    }
    // A light path entry must never stand in for full file metadata. Once
    // loaded, the full index also carries any publication/mutation updates.
    Snapshot path(K parent) throws IOException {
        Snapshot full=snapshots.get(parent);if(full!=null)return full;
        Snapshot found=pathSnapshots.get(parent);
        if(found==null){found=new Snapshot(pathReader.children(parent));pathSnapshots.put(parent,found);}
        return found;
    }
    Snapshot at(K parent) throws IOException {
        Snapshot found=snapshots.get(parent);
        if(found==null){found=new Snapshot(reader.children(parent));snapshots.put(parent,found);}
        return found;
    }
    private static String key(String name){return name.toLowerCase(Locale.ROOT);}
}
