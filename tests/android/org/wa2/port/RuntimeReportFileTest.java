package org.wa2.port;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
public final class RuntimeReportFileTest {
    private static void check(boolean condition){if(!condition)throw new AssertionError();}
    public static void main(String[] args)throws Exception{
        File directory=Files.createTempDirectory(Paths.get(args[0]).getParent(),"report-choice-").toFile();
        File full=new File(directory,"wa2-runtime.json"),progress=new File(directory,"wa2-runtime-progress.json");int cases=0;
        check(RuntimeReportFile.latest(directory).equals(full));++cases;
        Files.write(full.toPath(),"full".getBytes(StandardCharsets.UTF_8));check(full.setLastModified(10000));check(RuntimeReportFile.latest(directory).equals(full));++cases;
        Files.write(progress.toPath(),"progress".getBytes(StandardCharsets.UTF_8));check(progress.setLastModified(20000));check(RuntimeReportFile.latest(directory).equals(progress));++cases;
        check(full.setLastModified(30000));check(RuntimeReportFile.latest(directory).equals(full));++cases;
        check(progress.setLastModified(30000));check(RuntimeReportFile.latest(directory).equals(full));++cases;
        check(full.delete());check(RuntimeReportFile.latest(directory).equals(progress));++cases;
        Files.write(Paths.get(args[0]),("{\"schema\":\"wa2.runtime-report-choice.v1\",\"cases\":"+cases+",\"newest_private_report_selected\":true,\"complete_preferred_on_tie\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n").getBytes(StandardCharsets.UTF_8));
    }
}
