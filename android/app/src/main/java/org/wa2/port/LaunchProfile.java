package org.wa2.port;

import java.io.IOException;
import java.util.zip.CRC32;

/** Small, versioned adapter configuration. No game state or filesystem API. */
final class LaunchProfile {
    static final int FPS=1, LONG_PRESS=2, TEST_MODE=4;
    final int flags;
    LaunchProfile(int flags) { if(flags<0 || flags>7)throw new IllegalArgumentException("Unknown launcher setting");this.flags=flags; }
    static LaunchProfile defaults() { return new LaunchProfile(FPS|LONG_PRESS); }
    boolean enabled(int flag) { return (flags&flag)!=0; }
    LaunchProfile with(int flag,boolean value) { if(flag!=FPS && flag!=LONG_PRESS && flag!=TEST_MODE)throw new IllegalArgumentException("Unknown launcher setting");return new LaunchProfile(value?flags|flag:flags&~flag); }
    byte[] encode() {
        byte[] data=new byte[16];data[0]='W';data[1]='A';data[2]='L';data[3]='S';data[4]=1;data[5]=(byte)flags;
        CRC32 crc=new CRC32();crc.update(data,0,12);long sum=crc.getValue();for(int n=0;n<4;++n)data[12+n]=(byte)(sum>>(n*8));return data;
    }
    static LaunchProfile decode(byte[] data) throws IOException {
        if(data==null || data.length!=16 || data[0]!='W' || data[1]!='A' || data[2]!='L' || data[3]!='S' || data[4]!=1 || (data[5]&255)>7)throw new IOException("Unsupported launcher configuration");
        for(int n=6;n<12;++n)if(data[n]!=0)throw new IOException("Unknown launcher configuration fields");
        CRC32 crc=new CRC32();crc.update(data,0,12);long expected=0;for(int n=0;n<4;++n)expected|=(data[12+n]&255L)<<(n*8);if(crc.getValue()!=expected)throw new IOException("Damaged launcher configuration");return new LaunchProfile(data[5]);
    }
}
