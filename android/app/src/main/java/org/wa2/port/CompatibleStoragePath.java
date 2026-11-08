package org.wa2.port;

import java.io.IOException;
import java.util.Locale;

/** File-access bounds only. Source aliases and save formats stay in common C++. */
final class CompatibleStoragePath {
    static boolean valid(String path,boolean directory) {
        if(path==null || path.length()>160 || path.indexOf('\\')>=0 || path.indexOf('\0')>=0 || path.startsWith("/") || path.endsWith("/") || path.contains("//"))return false;
        String value=path.toLowerCase(Locale.ROOT);
        if(value.equals("test-mode"))return directory;
        if(value.startsWith("test-mode/"))value=value.substring(10);
        if(directory)return value.isEmpty() || value.equals("documents") || value.equals("documents/leaf") ||
            value.equals("documents/leaf/white album2") || value.equals("documents/leaf/white album2 -closing chapter-") || value.equals("documents/leaf/white album2 -introductory chapter-");
        int slash=value.lastIndexOf('/');String name=slash<0?value:value.substring(slash+1),parent=slash<0?"":value.substring(0,slash);
        if(!parent.isEmpty() && !(parent.equals("documents/leaf/white album2") || parent.equals("documents/leaf/white album2 -closing chapter-") || parent.equals("documents/leaf/white album2 -introductory chapter-")))return false;
        return name.equals("sys.sav") || name.equals("system.ini") || name.equals("d3d.ini") || name.equals("default.ini") || name.equals("save_q.sav") ||
            (name.length()==11 && name.startsWith("save_") && name.endsWith(".sav") && Character.isDigit(name.charAt(5)) && name.charAt(5)<128 && Character.isDigit(name.charAt(6)) && name.charAt(6)<128);
    }
    static String[] parts(String path,boolean directory) throws IOException {
        if(!valid(path,directory))throw new IOException("Unsupported selected-directory save/configuration path: "+path);
        return path.isEmpty()?new String[0]:path.split("/");
    }
    private CompatibleStoragePath() { }
}
