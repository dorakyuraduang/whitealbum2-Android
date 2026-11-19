package org.wa2.port;

public final class TextWorkWindowsTest {
    private static void check(boolean condition) {
        if (!condition) throw new AssertionError("Text work window mismatch");
    }
    private static final class Feed {
        final TextWorkWindows work = new TextWorkWindows();
        long frames, glyphs, tiles, wall, cpu;
        TextWorkWindows.Window step(int glyphCount, int tileCount, boolean clockAvailable) {
            frames+=25; glyphs+=25L*glyphCount; tiles+=25L*tileCount;
            wall+=250_000_000;cpu+=100_000_000;
            return work.observe(frames,glyphs,tiles,wall,clockAvailable ? cpu : -1);
        }
    }
    public static void main(String[] args) throws java.io.IOException {
        Feed f=new Feed();
        for(int i=0;i<5;++i) check(f.step(48,59,true)==null);
        f.work.foreground(true);
        check(f.step(50,60,true)==null);
        for(int i=0;i<3;++i) f.step(50,60,true);
        TextWorkWindows.Window[] light=f.work.takeSummary();
        check(light!=null && light[1]==null && light[0].frames==75 && light[0].glyphs==3750
                && light[0].tiles==4500 && light[0].wallNs==750_000_000 && light[0].cpuNs==300_000_000);
        check(f.work.takeSummary()==null);

        // The clicked-through transition cannot stand in for a settled page.
        for(int glyphs : new int[]{180,257,300,340,385,385}) f.step(glyphs,glyphs+15,true);
        check(f.work.takeSummary()==null);
        f.step(385,400,false);
        TextWorkWindows.Window[] full=f.work.takeSummary();
        check(full!=null && full[0]==light[0] && full[1].glyphs==75*385 && full[1].cpuNs==-1);
        check(full[0].segment==full[1].segment);
        check(f.step(385,400,true).cpuNs==-1);
        check(f.step(385,400,true).cpuNs==100_000_000);
        f.work.takeSummary();

        f.work.foreground(false);
        check(f.step(500,600,true)==null);
        f.work.foreground(true);
        check(f.step(500,600,true)==null);
        f.step(500,600,true);f.step(500,600,true);
        check(f.work.takeSummary()==null);
        f.step(500,600,true);
        check(f.work.takeSummary()[1].segment!=full[1].segment);
        f.wall+=2_000_000_000;
        check(f.step(500,600,true)==null);
        f.step(500,600,true);f.step(500,600,true);
        check(f.work.takeSummary()==null);
        f.frames=0;f.glyphs=0;f.tiles=0;
        check(f.step(500,600,true)==null);
        f.work.reset();f.work.foreground(true);
        for(int i=0;i<5;++i) f.step(0,0,true);
        check(f.work.takeSummary()==null);
        f.work.modeChanged(false);
        check(f.step(500,600,true)==null);
        for(int i=0;i<4;++i) check(f.step(500,600,true)!=null);
        check(f.work.takeSummary()==null);
        f.work.modeChanged(true);
        check(f.step(500,600,true)==null);
        for(int i=0;i<3;++i) f.step(500,600,true);
        check(f.work.takeSummary()[1].frames==75);
        if(args.length==1)java.nio.file.Files.write(java.nio.file.Paths.get(args[0]),
                "{\"passed\":true,\"game_launched\":false,\"screen_access\":false}\n".getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }
}
