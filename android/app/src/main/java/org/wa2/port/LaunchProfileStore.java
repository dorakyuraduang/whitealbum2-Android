package org.wa2.port;

import java.io.IOException;

/** I/O callers serialize load/flush; UI edits never wait for provider I/O. */
final class LaunchProfileStore {
    interface Storage { byte[] read() throws IOException;void replace(byte[] bytes) throws IOException; }
    private final Storage storage;
    private LaunchProfile current;
    private boolean dirty;
    private long revision;
    LaunchProfileStore(Storage storage,LaunchProfile fallback) throws IOException {
        if(storage==null || fallback==null)throw new IllegalArgumentException("Missing launcher storage");this.storage=storage;
        byte[] bytes=storage.read();current=bytes==null?fallback:LaunchProfile.decode(bytes);dirty=bytes==null;
    }
    synchronized LaunchProfile current() { return current; }
    synchronized boolean dirty() { return dirty; }
    synchronized boolean set(int flag,boolean value) {
        return update(current.with(flag,value));
    }
    synchronized boolean setArchive(boolean font,String name) { return update(current.withArchive(font,name)); }
    private boolean update(LaunchProfile next) {if(next.equals(current))return false;current=next;dirty=true;++revision;return true;}
    void flush() throws IOException {
        final LaunchProfile profile;final long version;
        synchronized(this){if(!dirty)return;profile=current;version=revision;}
        storage.replace(profile.encode());
        synchronized(this){if(revision==version)dirty=false;}
    }
    void reload() throws IOException {
        final long version;synchronized(this){if(dirty)throw new IOException("Launcher configuration still has unsaved edits");version=revision;}
        byte[] bytes=storage.read();LaunchProfile next=bytes==null?LaunchProfile.defaults():LaunchProfile.decode(bytes);
        synchronized(this){if(revision!=version)throw new IOException("Launcher configuration changed during reload");current=next;dirty=bytes==null;}
    }
}
