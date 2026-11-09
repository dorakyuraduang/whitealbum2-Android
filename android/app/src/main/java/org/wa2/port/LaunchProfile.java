package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.CRC32;

/** Bounded, versioned adapter configuration. No game state or filesystem API. */
final class LaunchProfile {
    static final int FPS=1, LONG_PRESS=2, TEST_MODE=4, CHINESE_SUBTITLES=8;
    static final int MAX_BYTES=16+128*2;
    final int flags;
    final String script, font;
    LaunchProfile(int flags) { this(flags,"",""); }
    LaunchProfile(int flags,String script,String font) {
        if(flags<0 || flags>15)throw new IllegalArgumentException("Unknown launcher setting");
        this.flags=flags;this.script=archive(script);this.font=archive(font);
    }
    // This is also the native mount's filename contract. An empty choice
    // leaves the player's current script/font resources and precedence alone.
    static String archive(String name) {
        if(name==null || name.length()>128)throw new IllegalArgumentException("Language PAK filename exceeds its bound");
        if(name.isEmpty())return name;
        for(int n=0;n<name.length();++n)if(name.charAt(n)>127)throw new IllegalArgumentException("语言包文件名请使用英文字母、数字、下划线或短横线");
        String value=name.toLowerCase(Locale.ROOT),leaf=value.startsWith("ic/")?value.substring(3):value;
        if(leaf.length()<5 || leaf.startsWith(".") || !leaf.endsWith(".pak"))throw new IllegalArgumentException("请选择原版格式的 PAK 脚本或字库");
        for(int n=0;n<leaf.length();++n){char c=leaf.charAt(n);if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))throw new IllegalArgumentException("语言包请使用英文字母、数字、下划线或短横线命名，放在游戏目录或 IC 内");}
        return value;
    }
    static LaunchProfile defaults() { return new LaunchProfile(FPS|LONG_PRESS|CHINESE_SUBTITLES); }
    boolean enabled(int flag) { return (flags&flag)!=0; }
    LaunchProfile with(int flag,boolean value) {
        if(flag!=FPS && flag!=LONG_PRESS && flag!=TEST_MODE && flag!=CHINESE_SUBTITLES)throw new IllegalArgumentException("Unknown launcher setting");
        return new LaunchProfile(value?flags|flag:flags&~flag,script,font);
    }
    LaunchProfile withArchive(boolean isFont,String name) { return new LaunchProfile(flags,isFont?script:name,isFont?name:font); }
    String[] gameArguments() {
        java.util.ArrayList<String> args=new java.util.ArrayList<>();
        if(enabled(TEST_MODE))args.add("--test-mode");
        if(!enabled(CHINESE_SUBTITLES))args.add("--no-chinese-subtitles");
        if(!script.isEmpty()){args.add("--script-pak");args.add(script);}
        if(!font.isEmpty()){args.add("--font-pak");args.add(font);}
        return args.toArray(new String[0]);
    }
    @Override public boolean equals(Object other) { if(!(other instanceof LaunchProfile))return false;LaunchProfile p=(LaunchProfile)other;return flags==p.flags && script.equals(p.script) && font.equals(p.font); }
    @Override public int hashCode() { return Objects.hash(flags,script,font); }
    byte[] encode() {
        byte[] a=script.getBytes(StandardCharsets.US_ASCII),b=font.getBytes(StandardCharsets.US_ASCII),data=new byte[16+a.length+b.length];
        data[0]='W';data[1]='A';data[2]='L';data[3]='S';data[4]=2;data[5]=(byte)flags;
        data[8]=(byte)a.length;data[9]=(byte)(a.length>>8);data[10]=(byte)b.length;data[11]=(byte)(b.length>>8);
        System.arraycopy(a,0,data,12,a.length);System.arraycopy(b,0,data,12+a.length,b.length);
        CRC32 crc=new CRC32();crc.update(data,0,data.length-4);long sum=crc.getValue();for(int n=0;n<4;++n)data[data.length-4+n]=(byte)(sum>>(n*8));return data;
    }
    static LaunchProfile decode(byte[] data) throws IOException {
        if(data==null || data.length<16 || data.length>MAX_BYTES || data[0]!='W' || data[1]!='A' || data[2]!='L' || data[3]!='S')throw new IOException("Unsupported launcher configuration");
        CRC32 crc=new CRC32();crc.update(data,0,data.length-4);long expected=0;for(int n=0;n<4;++n)expected|=(data[data.length-4+n]&255L)<<(n*8);
        if(crc.getValue()!=expected)throw new IOException("Damaged launcher configuration");
        if(data[4]==1){
            if(data.length!=16 || (data[5]&255)>7)throw new IOException("Unsupported launcher configuration");
            for(int n=6;n<12;++n)if(data[n]!=0)throw new IOException("Unknown launcher configuration fields");
            // Older releases always displayed these captions. Migration must
            // preserve that default along with every existing launcher flag.
            return new LaunchProfile(data[5]|CHINESE_SUBTITLES);
        }
        if(data[4]!=2 || (data[5]&255)>15 || data[6]!=0 || data[7]!=0)throw new IOException("Unknown launcher configuration fields");
        int a=(data[8]&255)|((data[9]&255)<<8),b=(data[10]&255)|((data[11]&255)<<8);
        if(a>128 || b>128 || data.length!=16+a+b)throw new IOException("Invalid language PAK filename lengths");
        for(int n=12;n<data.length-4;++n)if(data[n]<0)throw new IOException("Language PAK filename must be ASCII");
        try{return new LaunchProfile(data[5],new String(data,12,a,StandardCharsets.US_ASCII),new String(data,12+a,b,StandardCharsets.US_ASCII));}
        catch(IllegalArgumentException error){throw new IOException("Invalid launcher language resources",error);}
    }
}
