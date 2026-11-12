package org.wa2.port;

import java.io.IOException;

/** Generic relative read-only document paths. Game groups are chosen in C++. */
final class LooseResourcePath {
    static String[] parts(String name,boolean directory) throws IOException {
        if(name==null || name.length()>1024)throw new IOException("Invalid loose resource path");
        if(name.isEmpty()){if(directory)return new String[0];throw new IOException("Missing loose resource filename");}
        String[] parts=name.split("/",-1);if(parts.length>8)throw new IOException("Loose resource path is too deep");
        for(String part:parts){
            if(part.isEmpty() || part.equals(".") || part.equals("..") || part.length()>255 || part.endsWith(".") || part.endsWith(" "))throw new IOException("Invalid loose resource path component");
            for(int n=0;n<part.length();++n){char c=part.charAt(n);if(c<32 || c==127 || "\\:<>\"|?*".indexOf(c)>=0)throw new IOException("Invalid loose resource path character");}
        }
        return parts;
    }
    static String fold(String name){StringBuilder result=new StringBuilder(name.length());for(int n=0;n<name.length();++n){char c=name.charAt(n);result.append(c>='A' && c<='Z'?(char)(c+32):c);}return result.toString();}
    private LooseResourcePath(){}
}
