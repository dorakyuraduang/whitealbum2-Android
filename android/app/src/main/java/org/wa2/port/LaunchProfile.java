package org.wa2.port;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.CRC32;

/** Bounded, versioned adapter configuration. No game state or filesystem API. */
final class LaunchProfile {
    static final int FPS=1, LONG_PRESS=2, TEST_MODE=4, CHINESE_SUBTITLES=8;
    static final int MAX_BYTES=20+128*2+4096;
    final int flags;
    final String script, font, subtitle;
    LaunchProfile(int flags) { this(flags,"",""); }
    LaunchProfile(int flags,String script,String font) {this(flags,script,font,"");}
    LaunchProfile(int flags,String script,String font,String subtitle) {
        if(flags<0 || flags>15)throw new IllegalArgumentException("Unknown launcher setting");
        this.flags=flags;this.script=archive(script);this.font=archive(font);this.subtitle=subtitleUri(subtitle);
    }
    static String subtitleUri(String value) {
        if(value==null || (!value.isEmpty() && (!value.startsWith("content://") || value.length()<=10)) || value.getBytes(StandardCharsets.UTF_8).length>4096 || !new String(value.getBytes(StandardCharsets.UTF_8),StandardCharsets.UTF_8).equals(value))throw new IllegalArgumentException("Invalid external subtitle document URI");
        for(int at=0;at<value.length();++at)if(value.charAt(at)<32 || value.charAt(at)==127)throw new IllegalArgumentException("Control character in subtitle document URI");return value;
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
        return new LaunchProfile(value?flags|flag:flags&~flag,script,font,subtitle);
    }
    LaunchProfile withArchive(boolean isFont,String name) { return new LaunchProfile(flags,isFont?script:name,isFont?name:font,subtitle); }
    LaunchProfile withSubtitle(String uri) {return new LaunchProfile(flags,script,font,uri);}
    String[] gameArguments() {
        java.util.ArrayList<String> args=new java.util.ArrayList<>();
        if(enabled(TEST_MODE))args.add("--test-mode");
        if(!enabled(CHINESE_SUBTITLES))args.add("--no-chinese-subtitles");
        if(!script.isEmpty()){args.add("--script-pak");args.add(script);}
        if(!font.isEmpty()){args.add("--font-pak");args.add(font);}
        return args.toArray(new String[0]);
    }
    @Override public boolean equals(Object other) { if(!(other instanceof LaunchProfile))return false;LaunchProfile p=(LaunchProfile)other;return flags==p.flags && script.equals(p.script) && font.equals(p.font) && subtitle.equals(p.subtitle); }
    @Override public int hashCode() { return Objects.hash(flags,script,font,subtitle); }
    byte[] encode() {
        byte[] a=script.getBytes(StandardCharsets.US_ASCII),b=font.getBytes(StandardCharsets.US_ASCII),c=subtitle.getBytes(StandardCharsets.UTF_8),data=new byte[20+a.length+b.length+c.length];
        data[0]='W';data[1]='A';data[2]='L';data[3]='S';data[4]=3;data[5]=(byte)flags;
        data[8]=(byte)a.length;data[9]=(byte)(a.length>>8);data[10]=(byte)b.length;data[11]=(byte)(b.length>>8);
        data[12]=(byte)c.length;data[13]=(byte)(c.length>>8);
        System.arraycopy(a,0,data,16,a.length);System.arraycopy(b,0,data,16+a.length,b.length);System.arraycopy(c,0,data,16+a.length+b.length,c.length);
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
        if((data[4]!=2 && data[4]!=3) || (data[5]&255)>15 || data[6]!=0 || data[7]!=0)throw new IOException("Unknown launcher configuration fields");
        int a=(data[8]&255)|((data[9]&255)<<8),b=(data[10]&255)|((data[11]&255)<<8);
        int offset=data[4]==3?16:12,c=0;if(data[4]==3){if(data.length<20 || data[14]!=0 || data[15]!=0)throw new IOException("Invalid subtitle configuration fields");c=(data[12]&255)|((data[13]&255)<<8);}
        if(a>128 || b>128 || c>4096 || data.length!=offset+4+a+b+c)throw new IOException("Invalid launcher filename lengths");
        for(int n=offset;n<offset+a+b;++n)if(data[n]<0)throw new IOException("Language PAK filename must be ASCII");
        String uri=new String(data,offset+a+b,c,StandardCharsets.UTF_8);if(!java.util.Arrays.equals(uri.getBytes(StandardCharsets.UTF_8),java.util.Arrays.copyOfRange(data,offset+a+b,data.length-4)))throw new IOException("Invalid UTF-8 subtitle document URI");
        try{return new LaunchProfile(data[5],new String(data,offset,a,StandardCharsets.US_ASCII),new String(data,offset+a,b,StandardCharsets.US_ASCII),uri);}
        catch(IllegalArgumentException error){throw new IOException("Invalid launcher language resources",error);}
    }
}
