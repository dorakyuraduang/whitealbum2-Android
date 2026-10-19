package org.wa2.port;

/** Bounded source dialog icon decoding. No Android dependency or game logic. */
final class WidgetIcon {
    static boolean isIcon(int style) {
        int kind=style&31;
        if(kind==3)return true; // SS_ICON, not a text static.
        if(kind<=2 || kind==11 || kind==12 || kind==13 || kind==16 || kind==17 || kind==18)return false;
        throw new IllegalArgumentException("Unsupported source static style " + kind);
    }
    static final class Image {
        final int width,height;final int[] argb;
        Image(int width,int height,int[] argb){this.width=width;this.height=height;this.argb=argb;}
    }
    private static int word(byte[] data,int at) { return (data[at]&255)|((data[at+1]&255)<<8); }
    private static int dword(byte[] data,int at) { return word(data,at)|(word(data,at+2)<<16); }
    static Image decode(int width,int height,String hex) {
        if(width<1 || width>256 || height<1 || height>256 || hex==null || (hex.length()&1)!=0 || hex.length()>2097152)
            throw new IllegalArgumentException("Invalid source icon dimensions or payload");
        byte[] data=new byte[hex.length()/2];
        for(int n=0;n<data.length;++n){int high=Character.digit(hex.charAt(n*2),16),low=Character.digit(hex.charAt(n*2+1),16);
            if(high<0 || low<0)throw new IllegalArgumentException("Invalid source icon hex");data[n]=(byte)((high<<4)|low);}
        if(data.length<40)throw new IllegalArgumentException("Truncated source icon header");
        int header=dword(data,0),maskStride=((width+31)/32)*4;
        if((header!=40 && header!=108 && header!=124) || dword(data,4)!=width || dword(data,8)!=height*2 || word(data,12)!=1 || word(data,14)!=32 || dword(data,16)!=0 || dword(data,32)!=0)
            throw new IllegalArgumentException("Unsupported source icon bitmap format");
        int mask=header+width*height*4;
        if(data.length<mask+maskStride*height)throw new IllegalArgumentException("Truncated source icon pixels or mask");
        boolean alpha=false;
        for(int at=header+3;at<mask;at+=4)if(data[at]!=0){alpha=true;break;}
        int[] pixels=new int[width*height];
        for(int y=0;y<height;++y){int row=height-1-y;
            for(int x=0;x<width;++x){int at=header+(row*width+x)*4,colour=dword(data,at);
                int a=alpha?(colour>>>24):((data[mask+row*maskStride+x/8]&(0x80>>(x&7)))!=0?0:255);
                pixels[y*width+x]=(colour&0xffffff)|(a<<24);}}
        return new Image(width,height,pixels);
    }
    private WidgetIcon() { }
}
