package org.wa2.port;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;

/** Bounded name/metadata indexes, never document contents or descriptors. */
final class ReadOnlyDirectoryIndex<K,T> {
    interface Reader<K,T>{List<T> read(K directory) throws IOException;}
    interface Name<T>{String name(T entry);}
    interface Clock{long now();}
    static final int MAX_ENTRIES=65536,MAX_FOLDERS=8;
    static final long MAX_AGE_NS=250000000L;
    final class Snapshot {
        final List<T> entries;final HashMap<String,T> names=new HashMap<>();final HashSet<String> ambiguous=new HashSet<>();final long created;
        Snapshot(List<T> source,long now) throws IOException {
            if(source==null || source.size()>MAX_ENTRIES)throw new IOException("Loose resource directory exceeds 65536 entries");
            entries=new ArrayList<>(source);created=now;
            for(T item:entries){String name=naming.name(item);if(name==null)throw new IOException("Missing loose resource filename");String key=LooseResourcePath.fold(name);if(names.put(key,item)!=null)ambiguous.add(key);}
        }
        T find(String name) throws IOException {String key=LooseResourcePath.fold(name);if(ambiguous.contains(key))throw new IOException("Ambiguous loose resource filename: "+name);return names.get(key);}
    }
    private final Reader<K,T> reader;private final Name<T> naming;private final Clock clock;
    private final LinkedHashMap<K,Snapshot> roots=new LinkedHashMap<>(),folders=new LinkedHashMap<>(16,.75f,true);
    ReadOnlyDirectoryIndex(Reader<K,T> reader,Name<T> naming,Clock clock){this.reader=reader;this.naming=naming;this.clock=clock;}
    void clear(){roots.clear();folders.clear();}
    int cachedFolders(){return folders.size();}
    int cachedEntries(){int count=0;for(Snapshot s:folders.values())count+=s.entries.size();return count;}
    Snapshot at(K parent,boolean root) throws IOException {
        LinkedHashMap<K,Snapshot> cache=root?roots:folders;long now=clock.now();Snapshot found=cache.get(parent);
        if(found!=null){if(root || (now>=found.created && now-found.created<MAX_AGE_NS))return found;cache.remove(parent);}
        Snapshot next=new Snapshot(reader.read(parent),now);
        if(root){if(cache.size()>=2)throw new IOException("Too many loose resource root indexes");}
        else while(!cache.isEmpty() && (cache.size()>=MAX_FOLDERS || cachedEntries()+next.entries.size()>MAX_ENTRIES)){Iterator<K> oldest=cache.keySet().iterator();oldest.next();oldest.remove();}
        cache.put(parent,next);return next;
    }
}
