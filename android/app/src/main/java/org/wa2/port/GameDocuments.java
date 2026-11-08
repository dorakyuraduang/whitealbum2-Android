package org.wa2.port;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;

/** OS file access only; game save formats and import policy stay in common C++. */
final class GameDocuments {
    private final ContentResolver resolver;
    private final Uri tree;
    private static final String[] FIELDS = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE, DocumentsContract.Document.COLUMN_LAST_MODIFIED,
        DocumentsContract.Document.COLUMN_FLAGS };
    static final class Node {
        final Uri uri; final String name; final boolean directory; final long size, modified; final int flags;
        Node(Uri uri, String name, boolean directory, long size, long modified, int flags) {
            this.uri=uri; this.name=name; this.directory=directory; this.size=size; this.modified=modified; this.flags=flags;
        }
    }
    GameDocuments(ContentResolver resolver, Uri tree) { this.resolver=resolver; this.tree=tree; }
    private Node node(Cursor cursor) throws IOException {
        String id=cursor.getString(0), name=cursor.getString(1);
        if(id==null || name==null)throw new IOException("Incomplete game-directory document metadata");
        boolean directory=DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2));
        return new Node(DocumentsContract.buildDocumentUriUsingTree(tree,id),name,directory,
            cursor.isNull(3)?-1:cursor.getLong(3),cursor.isNull(4)?0:Math.max(0,cursor.getLong(4)),cursor.isNull(5)?0:cursor.getInt(5));
    }
    private Node inspect(Uri uri) throws IOException {
        try(Cursor cursor=resolver.query(uri,FIELDS,null,null,null)) {
            if(cursor==null || !cursor.moveToFirst())throw new IOException("Cannot inspect game-directory document");
            Node result=node(cursor);if(cursor.moveToNext())throw new IOException("Ambiguous game-directory document");return result;
        }
    }
    private ArrayList<Node> children(Uri parent) throws IOException {
        ArrayList<Node> result=new ArrayList<>();
        Uri uri=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(parent));
        try(Cursor cursor=resolver.query(uri,FIELDS,null,null,null)) {
            if(cursor==null)throw new IOException("Cannot inspect selected game directory");
            while(cursor.moveToNext()){if(result.size()>=10000)throw new IOException("Game directory has too many entries");result.add(node(cursor));}
        }
        return result;
    }
    private Node child(Uri parent,String name) throws IOException {
        Node result=null;for(Node item:children(parent))if(item.name.equalsIgnoreCase(name)) {
            if(result!=null)throw new IOException("Ambiguous game storage path: "+name);result=item;
        }
        return result;
    }
    private Uri parent(String[] parts,boolean create) throws IOException {
        Uri parent=DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree));
        for(int n=0;n+1<parts.length;++n) {
            Node folder=child(parent,parts[n]);
            if(folder==null && create) {
                Uri made=DocumentsContract.createDocument(resolver,parent,DocumentsContract.Document.MIME_TYPE_DIR,parts[n]);
                if(made==null)throw new IOException("Cannot create game save directory");folder=inspect(made);
                if(!folder.name.equals(parts[n]))throw new IOException("Provider changed game save directory name");
            }
            if(folder==null)return null;if(!folder.directory)throw new IOException("Game save directory has the wrong file type");parent=folder.uri;
        }
        return parent;
    }
    private final class Files implements GameFilePublication.Backend<Node> {
        final Uri parent;final HashMap<String,Node> index=new HashMap<>();final HashSet<String> ambiguous=new HashSet<>();
        Files(Uri parent) throws IOException {
            this.parent=parent;
            for(Node file:children(parent)) {
                String key=file.name.toLowerCase(Locale.ROOT);
                if(index.put(key,file)!=null)ambiguous.add(key);
            }
        }
        @Override public Node find(String name) throws IOException {String key=name.toLowerCase(Locale.ROOT);if(ambiguous.contains(key))throw new IOException("Ambiguous game storage filename: "+name);Node file=index.get(key);if(file!=null && file.directory)throw new IOException("Game save is a directory: "+name);return file;}
        @Override public Node create(String name) throws IOException {
            if(find(name)!=null)throw new IOException("Game save temporary already exists");
            Uri uri=DocumentsContract.createDocument(resolver,parent,"application/octet-stream",name);if(uri==null)throw new IOException("Cannot create game save temporary");Node file=inspect(uri);
            if(!file.name.equals(name)){try{DocumentsContract.deleteDocument(resolver,uri);}catch(Exception ignored){}throw new IOException("Provider changed game save filename");}index.put(name.toLowerCase(Locale.ROOT),file);return file;
        }
        @Override public Node rename(Node file,String name) throws IOException {
            if((file.flags&DocumentsContract.Document.FLAG_SUPPORTS_RENAME)==0)throw new IOException("Selected game directory does not support safe save renaming");
            if(find(name)!=null)throw new IOException("Game save publication target already exists");
            Uri renamed=DocumentsContract.renameDocument(resolver,file.uri,name);if(renamed==null)throw new IOException("Cannot publish game save");Node result=inspect(renamed);
            if(!result.name.equals(name))throw new IOException("Provider changed published game save name");index.remove(file.name.toLowerCase(Locale.ROOT));index.put(name.toLowerCase(Locale.ROOT),result);return result;
        }
        @Override public void remove(Node file) throws IOException {if(!DocumentsContract.deleteDocument(resolver,file.uri))throw new IOException("Cannot remove owned game save temporary");index.remove(file.name.toLowerCase(Locale.ROOT));}
        @Override public void write(Node file,ByteBuffer bytes) throws IOException {writeFile(file,bytes,-1,0);}
    }
    private void writeFile(Node file,ByteBuffer bytes,long expected,long offset) throws IOException {
        if((file.flags&DocumentsContract.Document.FLAG_SUPPORTS_WRITE)==0)throw new IOException("Selected game directory is not writable");
        ParcelFileDescriptor fd=resolver.openFileDescriptor(file.uri,expected<0?"w":"rw");if(fd==null)throw new IOException("Cannot open writable game save");
        try(ParcelFileDescriptor.AutoCloseOutputStream output=new ParcelFileDescriptor.AutoCloseOutputStream(fd)) {
            FileChannel channel=output.getChannel();if(expected>=0 && channel.size()!=expected)throw new IOException("Game save changed size before update");channel.position(offset);
            while(bytes.hasRemaining())if(channel.write(bytes)<=0)throw new IOException("Short game save write");channel.force(true);
            if(expected>=0 && channel.size()!=expected)throw new IOException("Game save changed size during update");
        }
    }
    int open(String path) throws IOException {
        String[] parts=GameStoragePath.parts(path,false);Uri parent=parent(parts,false);if(parent==null)return -1;
        // Only writable native-owned paths have publication sidecars. Original
        // PC files under Documents stay strictly read-only.
        Files files=new Files(parent);String name=parts[parts.length-1];Node file=GameStoragePath.valid(path,true)?GameFilePublication.recover(files,name):files.find(name);if(file==null)return -1;
        try(ParcelFileDescriptor fd=resolver.openFileDescriptor(file.uri,"r")){if(fd==null)throw new IOException("Cannot open selected game save");return fd.detachFd();}
    }
    void replace(String path,ByteBuffer bytes) throws IOException {
        String[] parts=GameStoragePath.parts(path,true);if(bytes==null || bytes.remaining()>64*1024*1024)throw new IOException("Game save exceeds write bound");
        GameFilePublication.replace(new Files(parent(parts,true)),parts[parts.length-1],bytes);
    }
    void update(String path,long expected,long offset,ByteBuffer bytes) throws IOException {
        String[] parts=GameStoragePath.parts(path,true);if(bytes==null || expected<0 || expected>64*1024*1024 || offset<0 || offset>expected || bytes.remaining()>expected-offset)throw new IOException("Invalid game save update range");
        Uri parent=parent(parts,false);if(parent==null)throw new IOException("Missing game save directory");Node file=GameFilePublication.recover(new Files(parent),parts[parts.length-1]);if(file==null || file.size!=expected)throw new IOException("Game save update requires an existing complete file");writeFile(file,bytes.asReadOnlyBuffer(),expected,offset);
    }
    String[] saves(String directory) throws IOException {
        if(!directory.equals("native-saves") && !directory.equals("native-test/native-saves"))throw new IOException("Invalid native save inventory directory");
        Uri parent=parent((directory+"/slot-000.wa2sav").split("/"),false);if(parent==null)return new String[0];
        LinkedHashSet<String> names=new LinkedHashSet<>();for(Node item:children(parent)) {
            String name=item.name;if(name.endsWith(GameFilePublication.STAGE))name=name.substring(0,name.length()-GameFilePublication.STAGE.length());else if(name.endsWith(GameFilePublication.BACKUP))name=name.substring(0,name.length()-GameFilePublication.BACKUP.length());
            if(GameStoragePath.slot(name))names.add(name);
        }
        ArrayList<String> result=new ArrayList<>();Files files=new Files(parent);for(String name:names) {
            Node file=GameFilePublication.recover(files,name);if(file==null)continue;
            result.add(name);result.add(Long.toString(Math.max(0,file.size)));result.add(Long.toString(file.modified));
        }
        return result.toArray(new String[0]);
    }
}
