package org.wa2.port;

import java.io.File;

/** Private counter reports only; never scans the selected game directory. */
final class RuntimeReportFile {
    static File latest(File directory){
        File complete=new File(directory,"wa2-runtime.json"),progress=new File(directory,"wa2-runtime-progress.json");
        return progress.isFile() && (!complete.isFile() || progress.lastModified()>complete.lastModified())?progress:complete;
    }
    private RuntimeReportFile(){}
}
