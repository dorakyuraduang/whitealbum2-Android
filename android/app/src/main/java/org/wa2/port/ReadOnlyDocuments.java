package org.wa2.port;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Authorized-tree read-only adapter. No extraction, recovery or publication. */
final class ReadOnlyDocuments {
    private static final String[] FIELDS={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,DocumentsContract.Document.COLUMN_SIZE,DocumentsContract.Document.COLUMN_LAST_MODIFIED};
    private static final class Node {
        final Uri uri;final String name;final boolean directory;final long size,modified;
        Node(Uri uri,String name,boolean directory,long size,long modified){this.uri=uri;this.name=name;this.directory=directory;this.size=size;this.modified=modified;}
    }
    private final ContentResolver resolver;private final Uri tree;private final Node root;
    private final ReadOnlyDirectoryIndex<Uri,Node> index;
    ReadOnlyDocuments(ContentResolver resolver,Uri tree){this.resolver=resolver;this.tree=tree;
        root=new Node(DocumentsContract.buildDocumentUriUsingTree(tree,DocumentsContract.getTreeDocumentId(tree)),"",true,0,0);
        index=new ReadOnlyDirectoryIndex<>(this::children,node->node.name,System::nanoTime);
    }
    boolean belongsTo(Uri value){return tree.equals(value);}
    private List<Node> children(Uri parent) throws IOException {
        ArrayList<Node> result=new ArrayList<>();Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,DocumentsContract.getDocumentId(parent));
        try(Cursor cursor=resolver.query(children,FIELDS,null,null,null)){
            if(cursor==null)throw new IOException("Cannot inspect loose resource directory");
            while(cursor.moveToNext()){
                if(result.size()>=ReadOnlyDirectoryIndex.MAX_ENTRIES)throw new IOException("Loose resource directory exceeds 65536 entries");
                String id=cursor.getString(0),name=cursor.getString(1);if(id==null || name==null)throw new IOException("Incomplete loose resource metadata");
                boolean directory=DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2));
                result.add(new Node(DocumentsContract.buildDocumentUriUsingTree(tree,id),name,directory,
                    directory || cursor.isNull(3)?0:Math.max(0,cursor.getLong(3)),cursor.isNull(4)?0:Math.max(0,cursor.getLong(4))));
            }
        }
        return result;
    }
    private Node resolve(String[] parts) throws IOException {
        Node current=root;
        for(int n=0;n<parts.length;++n){if(!current.directory)return null;
            boolean fixed=n==0 || (n==1 && LooseResourcePath.fold(parts[0]).equals("ic"));
            current=index.at(current.uri,fixed).find(parts[n]);if(current==null)return null;
        }
        return current;
    }
    private Node settled(String[] parts) throws IOException {
        try{return resolve(parts);}catch(FileNotFoundException missing){index.clear();return resolve(parts);}
    }
    private static void record(ArrayList<String> result,Node node){result.add(node.name);result.add(Long.toString(node.size));result.add(Long.toString(node.modified));result.add(node.directory?"16":"128");}
    synchronized String[] metadata(String name,boolean list) throws IOException {
        String[] parts=LooseResourcePath.parts(name,list);Node file=settled(parts);ArrayList<String> result=new ArrayList<>();
        if(file!=null){if(list){if(file.directory){boolean fixed=parts.length==0 || (parts.length==1 && LooseResourcePath.fold(parts[0]).equals("ic"));for(Node child:index.at(file.uri,fixed).entries)record(result,child);}}
            else record(result,file);}
        return result.toArray(new String[0]);
    }
    synchronized int open(String name) throws IOException {
        String[] parts=LooseResourcePath.parts(name,false);
        for(int attempt=0;attempt<2;++attempt){Node file=settled(parts);if(file==null)return -1;if(file.directory)throw new IOException("Loose resource is a directory: "+name);
            try(ParcelFileDescriptor descriptor=resolver.openFileDescriptor(file.uri,"r")){if(descriptor==null)throw new IOException("Cannot open loose resource: "+name);return descriptor.detachFd();}
            catch(FileNotFoundException missing){index.clear();if(attempt!=0)throw missing;}
        }
        throw new IOException("Cannot resolve loose resource: "+name);
    }
}
