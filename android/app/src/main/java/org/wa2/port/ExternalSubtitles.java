package org.wa2.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Looper;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import org.libsdl.app.SDL;

/** Document and off-screen font adapters. Parsing/timing use the shared C++. */
final class ExternalSubtitles {
    private ExternalSubtitles() { }
    private static boolean loaded;
    private static synchronized void loadValidator() {
        if(loaded)return;
        // Loading exports never calls SDL_main, initializes a device or starts
        // an activity. SDLActivity performs its normal setup when game starts.
        for(String library:PortUi.libraries())System.loadLibrary(library);
        loaded=true;
    }
    private static native String nativeValidate(byte[] bytes) throws IOException;
    static String validate(Context context,String location) throws IOException {
        byte[] bytes=read(context,location);loadValidator();return nativeValidate(bytes);
    }
    static byte[] read(Context context,String location) throws IOException {
        if(Looper.myLooper()==Looper.getMainLooper())throw new IOException("字幕文件必须在后台读取");
        final Uri uri=Uri.parse(LaunchProfile.subtitleUri(location));if(!"content".equals(uri.getScheme()))throw new IOException("请选择系统文件选择器提供的字幕文件（sub.yaml 或 Todokanai 格式）");
        ByteArrayOutputStream result=new ByteArrayOutputStream();byte[] buffer=new byte[8192];
        try(InputStream input=context.getContentResolver().openInputStream(uri)){
            if(input==null)throw new IOException("无法打开外置字幕文件，请重新选择并授权");
            for(;;){int count=input.read(buffer);if(count<0)break;if(count==0)throw new IOException("无法完整读取外置字幕文件");if(count>1024*1024-result.size())throw new IOException("外置字幕不能超过 1 MiB");result.write(buffer,0,count);}
        }
        if(result.size()==0)throw new IOException("外置字幕文件为空");return result.toByteArray();
    }
    public static byte[] readForGame() throws IOException {
        Context context=SDL.getContext();if(!(context instanceof Wa2Activity))throw new IOException("字幕运行会话尚未准备好");
        if(!LaunchSettings.sessionValid((Wa2Activity)context))throw new IOException("字幕设置与当前游戏目录不匹配");
        String location=LaunchSettings.subtitle(context);return !LaunchSettings.chineseSubtitles(context) || location.isEmpty()?null:read(context,location);
    }
    static String display(String location) {
        if(location.isEmpty())return "内置字幕（默认）";
        String name=Uri.parse(location).getLastPathSegment();if(name==null || name.isEmpty())return "外置 sub.yaml";
        int slash=name.lastIndexOf('/');return "外置："+name.substring(slash+1);
    }
    // Returns count, then left/top/width/height/coverage for each character.
    // These pixels belong solely to new memory bitmaps; no screen is accessed.
    public static int[] rasterize(int[] characters) throws IOException {
        if(characters==null || characters.length==0 || characters.length>1536)throw new IOException("字幕字符数量超出范围");
        Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);paint.setColor(Color.WHITE);paint.setTypeface(Typeface.SANS_SERIF);paint.setTextSize(28);paint.setHinting(Paint.HINTING_ON);
        Rect[] bounds=new Rect[characters.length];String[] text=new String[characters.length];int total=1;
        for(int n=0;n<characters.length;++n){
            int cp=characters[n];if(!Character.isValidCodePoint(cp) || cp<32 || (cp>=0xd800 && cp<=0xdfff))throw new IOException("无效的字幕 Unicode 字符");
            text[n]=new String(Character.toChars(cp));if(!Character.isSpaceChar(cp) && !paint.hasGlyph(text[n]))throw new IOException(String.format(java.util.Locale.ROOT,"系统字幕字体缺少字符 U+%04X",cp));
            Rect box=new Rect();paint.getTextBounds(text[n],0,text[n].length(),box);
            if(box.width()>36 || box.height()>36 || box.left < -40 || box.top < -40 || box.right>40 || box.bottom>40)throw new IOException("字幕字形超出支持的字体尺寸");
            if(!Character.isSpaceChar(cp) && box.isEmpty())throw new IOException("字幕含不能显示的字符");
            bounds[n]=box;total+=4+box.width()*box.height();
        }
        int[] result=new int[total];result[0]=characters.length;int at=1;Bitmap bitmap=Bitmap.createBitmap(40,40,Bitmap.Config.ARGB_8888);Canvas canvas=new Canvas(bitmap);int[] pixels=new int[1600];
        try{for(int n=0;n<characters.length;++n){Rect box=bounds[n];int width=box.width(),height=box.height();result[at++]=box.left;result[at++]=box.top;result[at++]=width;result[at++]=height;
            if(width==0 || height==0)continue;
            bitmap.eraseColor(Color.TRANSPARENT);canvas.drawText(text[n],-box.left,-box.top,paint);bitmap.getPixels(pixels,0,width,0,0,width,height);
            for(int p=0;p<width*height;++p)result[at++]=pixels[p]>>>24;
        }}finally{bitmap.recycle();}
        return result;
    }
}
