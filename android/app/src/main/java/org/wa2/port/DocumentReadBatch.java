package org.wa2.port;

import java.io.IOException;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

/** Bounded read-only OS requests; no filenames, save formats or payload cache. */
final class DocumentReadBatch {
    static final int LIMIT=16, WORKERS=4;
    private static final ExecutorService workers=Executors.newFixedThreadPool(WORKERS, task->{
        Thread thread=new Thread(task,"WA2-document-read");thread.setDaemon(true);return thread;
    });
    interface Opener {int open(int index) throws IOException;void close(int descriptor);}
    static int[] open(int count,Opener opener) throws IOException {
        if(count<1 || count>LIMIT)throw new IOException("Invalid document read batch size");
        int[] descriptors=new int[count];Arrays.fill(descriptors,-1);
        CountDownLatch finished=new CountDownLatch(Math.min(count,WORKERS));
        AtomicInteger next=new AtomicInteger();int submitted=0;Throwable submissionFailure=null;
        try{
            for(;submitted<Math.min(count,WORKERS);++submitted)workers.execute(()->{
                try{for(int n;(n=next.getAndIncrement())<count;){
                    try{descriptors[n]=opener.open(n);}catch(IOException | RuntimeException unavailable){
                        // Speculation is optional. The normal named open will
                        // retry and report its own error if actually needed.
                        descriptors[n]=-1;
                    }
                }}finally{finished.countDown();}
            });
        }catch(RuntimeException | Error rejected){submissionFailure=rejected;while(submitted++<Math.min(count,WORKERS))finished.countDown();}
        boolean interrupted=false;
        for(;;)try{finished.await();break;}catch(InterruptedException stop){interrupted=true;}
        if(interrupted || submissionFailure!=null){
            for(int fd:descriptors)if(fd>=0)opener.close(fd);Arrays.fill(descriptors,-1);
            if(interrupted){Thread.currentThread().interrupt();throw new IOException("Document read batch interrupted");}
            if(submissionFailure instanceof Error)throw (Error)submissionFailure;
        }
        return descriptors;
    }
    private DocumentReadBatch(){}
}
