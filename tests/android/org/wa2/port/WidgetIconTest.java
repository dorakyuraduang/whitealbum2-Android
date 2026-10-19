package org.wa2.port;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

/** Runs production static classification and icon decoding without Android UI. */
public final class WidgetIconTest {
    static int checks,rejections;
    static void check(boolean value){if(!value)throw new AssertionError("Widget icon check " + checks);++checks;}
    interface Attempt {void run();}
    static void rejects(Attempt fn){boolean rejected=false;try{fn.run();}catch(IllegalArgumentException error){rejected=true;}check(rejected);++rejections;}
    static void put(byte[] data,int at,int value){for(int n=0;n<4;++n)data[at+n]=(byte)(value>>>(n*8));}
    static String hex(byte[] data){StringBuilder out=new StringBuilder(data.length*2);for(byte value:data){out.append("0123456789abcdef".charAt((value&255)>>>4));out.append("0123456789abcdef".charAt(value&15));}return out.toString();}
    public static void main(String[] args) throws Exception {
        int styles=0;
        for(int kind=0;kind<32;++kind)for(int flags:new int[]{0,0x50000000,0x50004000}){
            final int style=flags|kind;
            if(kind==3){check(WidgetIcon.isIcon(style));++styles;}
            else if(kind<=2 || kind==11 || kind==12 || kind==13 || kind==16 || kind==17 || kind==18){check(!WidgetIcon.isIcon(style));++styles;}
            else {rejects(() -> WidgetIcon.isIcon(style));++styles;}}
        // Three-pixel rows exercise DWORD-padded masks and bottom-up storage.
        int width=3,height=2;byte[] dib=new byte[40+width*height*4+8];put(dib,0,40);put(dib,4,width);put(dib,8,height*2);put(dib,12,1|(32<<16));
        int[] expected={0x00010203,0x7f405060,0xffabcdef,0xff123456,0x01223344,0x80aabbcc};
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)put(dib,40+((height-1-y)*width+x)*4,expected[y*width+x]);
        Arrays.fill(dib,64,dib.length,(byte)0xff); // Meaningful alpha overrides the legacy AND mask.
        WidgetIcon.Image alpha=WidgetIcon.decode(width,height,hex(dib));check(alpha.width==width && alpha.height==height && Arrays.equals(alpha.argb,expected));
        byte[] masked=dib.clone();for(int at=43;at<64;at+=4)masked[at]=0;
        Arrays.fill(masked,64,masked.length,(byte)0);masked[64]=(byte)0xa0;masked[68]=(byte)0x40;
        int[] fallback={0xff010203,0x00405060,0xffabcdef,0x00123456,0xff223344,0x00aabbcc};
        check(Arrays.equals(WidgetIcon.decode(width,height,hex(masked)).argb,fallback));
        for(int size=0;size<dib.length;++size){final byte[] prefix=Arrays.copyOf(dib,size);rejects(() -> WidgetIcon.decode(width,height,hex(prefix)));}
        for(int at:new int[]{0,4,8,12,14,16,32}){byte[] changed=dib.clone();changed[at]^=1;rejects(() -> WidgetIcon.decode(width,height,hex(changed)));}
        rejects(() -> WidgetIcon.decode(0,height,hex(dib)));rejects(() -> WidgetIcon.decode(width,257,hex(dib)));
        rejects(() -> WidgetIcon.decode(width,height,null));rejects(() -> WidgetIcon.decode(width,height,"f"));rejects(() -> WidgetIcon.decode(width,height,"zz"));
        rejects(() -> WidgetIcon.decode(width,height,new String(new char[2097154])));
        int cached=0,pixels=0;
        for(String line:Files.readAllLines(Path.of(args[1]))){if(line.isEmpty())continue;String[] fields=line.split("\t");check(fields.length==4);
            int w=Integer.parseInt(fields[0]),h=Integer.parseInt(fields[1]);WidgetIcon.Image image=WidgetIcon.decode(w,h,fields[2]);
            check(image.argb.length==w*h && fields[3].length()==w*h*8);
            for(int n=0;n<image.argb.length;++n){int original=(int)Long.parseLong(fields[3].substring(n*8,n*8+8),16);check(image.argb[n]==original);++pixels;}++cached;}
        check(cached==2 && pixels==2048 && styles==96);
        Files.writeString(Path.of(args[0]),"{\"schema\":\"wa2.widget-icons.v1\",\"checks\":"+checks+",\"style_cases\":"+styles+",\"rejected_inputs\":"+rejections
            +",\"cached_icons\":"+cached+",\"exact_cached_pixels\":"+pixels+",\"alpha_and_mask_checked\":true,\"bottom_up_rows_checked\":true,\"native_ui_created\":false,\"android_runtime_executed\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n");
        System.out.println("PASS source dialog icons: "+styles+" styles, "+pixels+" exact cached pixels and "+rejections+" rejections; no Android UI.");
    }
}
