package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.CRC32;

/** One language choice; resource pairs are selected by the common game host. */
final class LaunchProfile {
    static final int FPS=1, LONG_PRESS=2, TEST_MODE=4, CHINESE_SUBTITLES=8, HIDE_MENU=16;
    static final int CHINESE=0, JAPANESE=1, ENGLISH=2;
    private static final int VERSION=7;
    // Retain the larger legacy v3 read bound for migration.
    static final int MAX_BYTES=20+128*2+4096;
    final int flags, language;
    final String subtitle;
    LaunchProfile(int flags) {this(flags,CHINESE,"");}
    LaunchProfile(int flags,int language,String subtitle) {
        if(flags<0 || flags>31)throw new IllegalArgumentException("Unknown launcher setting");
        if(language!=CHINESE && language!=JAPANESE && language!=ENGLISH)throw new IllegalArgumentException("Unsupported game language");
        this.flags=flags;this.language=language;this.subtitle=subtitleUri(subtitle);
    }
    static String subtitleUri(String value) {
        if(value==null || (!value.isEmpty() && (!value.startsWith("content://") || value.length()<=10)) || value.getBytes(StandardCharsets.UTF_8).length>4096 || !new String(value.getBytes(StandardCharsets.UTF_8),StandardCharsets.UTF_8).equals(value))throw new IllegalArgumentException("Invalid external subtitle document URI");
        for(int at=0;at<value.length();++at)if(value.charAt(at)<32 || value.charAt(at)==127)throw new IllegalArgumentException("Control character in subtitle document URI");return value;
    }
    String languageTag(){return language==ENGLISH?"en-US":language==JAPANESE?"ja-JP":"zh-CN";}
    String languageLabel(){return language==ENGLISH?"English":language==JAPANESE?"日本語":"简体中文";}
    // These names are launcher metadata checks. The common C++ profile owns
    // actual archive routing and language-specific default-name data.
    String scriptArchive(){return language==ENGLISH?"en.pak":language==JAPANESE?"script.pak":"ck-gal.pak";}
    String fontArchive(){return language==ENGLISH?"fon_en.pak":language==JAPANESE?"fnt.pak":"fon.pak";}
    static LaunchProfile defaults(){return new LaunchProfile(FPS|LONG_PRESS|CHINESE_SUBTITLES);}
    boolean enabled(int flag){return (flags&flag)!=0;}
    LaunchProfile with(int flag,boolean value){
        if(flag!=FPS && flag!=LONG_PRESS && flag!=TEST_MODE && flag!=CHINESE_SUBTITLES && flag!=HIDE_MENU)throw new IllegalArgumentException("Unknown launcher setting");
        return new LaunchProfile(value?flags|flag:flags&~flag,language,subtitle);
    }
    LaunchProfile withLanguage(int value){return new LaunchProfile(flags,value,subtitle);}
    LaunchProfile withSubtitle(String uri){return new LaunchProfile(flags,language,uri);}
    String[] gameArguments(){
        java.util.ArrayList<String> args=new java.util.ArrayList<>();
        if(enabled(TEST_MODE))args.add("--test-mode");
        if(!enabled(CHINESE_SUBTITLES))args.add("--no-chinese-subtitles");
        args.add("--language");args.add(languageTag());return args.toArray(new String[0]);
    }
    @Override public boolean equals(Object other){if(!(other instanceof LaunchProfile))return false;LaunchProfile p=(LaunchProfile)other;return flags==p.flags && language==p.language && subtitle.equals(p.subtitle);}
    @Override public int hashCode(){return Objects.hash(flags,language,subtitle);}
    byte[] encode(){
        byte[] uri=subtitle.getBytes(StandardCharsets.UTF_8),data=new byte[16+uri.length];
        data[0]='W';data[1]='A';data[2]='L';data[3]='S';data[4]=VERSION;data[5]=(byte)flags;data[6]=(byte)language;
        data[8]=(byte)uri.length;data[9]=(byte)(uri.length>>8);System.arraycopy(uri,0,data,12,uri.length);
        CRC32 crc=new CRC32();crc.update(data,0,data.length-4);long sum=crc.getValue();for(int n=0;n<4;++n)data[data.length-4+n]=(byte)(sum>>(n*8));return data;
    }
    static boolean needsUpgrade(byte[] data){return data==null || data[4]!=VERSION;}
    private static String uri(byte[] data,int offset,int count)throws IOException{
        String value=new String(data,offset,count,StandardCharsets.UTF_8);
        if(!Arrays.equals(value.getBytes(StandardCharsets.UTF_8),Arrays.copyOfRange(data,offset,offset+count)))throw new IOException("Invalid UTF-8 subtitle document URI");return value;
    }
    private static String oldArchive(byte[] data,int offset,int count)throws IOException{
        if(count>128)throw new IOException("Legacy PAK filename exceeds its bound");
        String value=new String(data,offset,count,StandardCharsets.US_ASCII).toLowerCase(Locale.ROOT);
        if(value.isEmpty())return value;
        String leaf=value.startsWith("ic/")?value.substring(3):value;
        if(leaf.length()<5 || leaf.startsWith(".") || !leaf.endsWith(".pak"))throw new IOException("Invalid legacy PAK filename");
        for(int n=0;n<leaf.length();++n){char c=leaf.charAt(n);if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))throw new IOException("Invalid legacy PAK filename");}
        return value;
    }
    static LaunchProfile decode(byte[] data)throws IOException{
        if(data==null || data.length<16 || data.length>MAX_BYTES || data[0]!='W' || data[1]!='A' || data[2]!='L' || data[3]!='S')throw new IOException("Unsupported launcher configuration");
        CRC32 crc=new CRC32();crc.update(data,0,data.length-4);long expected=0;for(int n=0;n<4;++n)expected|=(data[data.length-4+n]&255L)<<(n*8);if(crc.getValue()!=expected)throw new IOException("Damaged launcher configuration");
        int version=data[4]&255,flags=data[5]&255;
        try{
            if(version==1){if(data.length!=16 || flags>7)throw new IOException("Unsupported legacy launcher configuration");for(int n=6;n<12;++n)if(data[n]!=0)throw new IOException("Unknown legacy launcher fields");return new LaunchProfile(flags|CHINESE_SUBTITLES);}
            if(flags>(version>=6 && version<=VERSION?31:15) || data[7]!=0)throw new IOException("Unknown launcher fields");
            if(version==4 || version==5 || version==6 || version==VERSION){
                int size=(data[8]&255)|((data[9]&255)<<8),language=data[6]&255;
                if(size>4096 || data.length!=16+size || data[10]!=0 || data[11]!=0 || (language!=CHINESE && language!=JAPANESE && !(version==VERSION && language==ENGLISH)))throw new IOException("Invalid language configuration fields");
                // v4 could infer Japanese from old filenames. Start older
                // profiles in Chinese once; v5+ retain the player's choice.
                // Older records have no hide-menu bit, so the menu stays shown.
                return new LaunchProfile(flags,version>=5?language:CHINESE,uri(data,12,size));
            }
            if((version!=2 && version!=3) || data[6]!=0)throw new IOException("Unsupported launcher configuration version");
            int a=(data[8]&255)|((data[9]&255)<<8),b=(data[10]&255)|((data[11]&255)<<8),c=0,offset=version==3?16:12;
            if(version==3){if(data.length<20 || data[14]!=0 || data[15]!=0)throw new IOException("Unknown legacy subtitle fields");c=(data[12]&255)|((data[13]&255)<<8);}
            if(a>128 || b>128 || c>4096 || data.length!=offset+4+a+b+c)throw new IOException("Invalid legacy launcher lengths");
            oldArchive(data,offset,a);oldArchive(data,offset+a,b);
            return new LaunchProfile(flags,CHINESE,uri(data,offset+a+b,c));
        }catch(IllegalArgumentException error){throw new IOException("Invalid launcher language configuration",error);}
    }
}
