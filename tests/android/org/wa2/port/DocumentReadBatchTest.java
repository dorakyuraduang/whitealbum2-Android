package org.wa2.port;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class DocumentReadBatchTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    static final class Provider implements DocumentReadBatch.Opener {
        final AtomicInteger calls=new AtomicInteger(),active=new AtomicInteger(),maximum=new AtomicInteger(),closed=new AtomicInteger();
        final CountDownLatch overlap=new CountDownLatch(4);boolean failures,rendezvous;int offset;
        public int open(int index)throws IOException {
            int now=active.incrementAndGet();maximum.accumulateAndGet(now,Math::max);calls.incrementAndGet();
            try{
                if(rendezvous && index<4){overlap.countDown();try{check(overlap.await(5,TimeUnit.SECONDS));}catch(InterruptedException stop){Thread.currentThread().interrupt();throw new IOException(stop);}}
                if(failures && index==3)throw new IOException("Synthetic missing document");
                if(failures && index==7)throw new SecurityException("Synthetic permission change");
                return 1000+offset+index;
            }finally{active.decrementAndGet();}
        }
        public void close(int descriptor){check(descriptor>=1000);closed.incrementAndGet();}
    }
    public static void main(String[] args)throws Exception {
        int cases=0,total=0;Provider provider=new Provider();provider.rendezvous=true;
        for(int first=0;first<100;first+=DocumentReadBatch.LIMIT){
            provider.offset=first;int count=Math.min(DocumentReadBatch.LIMIT,100-first);int[] batch=DocumentReadBatch.open(count,provider);
            for(int n=0;n<count;++n){check(batch[n]==1000+first+n);++cases;}total+=batch.length;provider.rendezvous=false;
        }
        check(total==100 && provider.calls.get()==100 && provider.maximum.get()==4 && provider.active.get()==0 && provider.closed.get()==0);++cases;
        Provider failed=new Provider();failed.failures=true;int[] partial=DocumentReadBatch.open(16,failed);
        for(int n=0;n<partial.length;++n){check(partial[n]==(n==3 || n==7?-1:1000+n));++cases;}
        check(failed.calls.get()==16 && failed.active.get()==0);++cases;
        Provider invalid=new Provider();for(int count:new int[]{0,-1,17}){boolean rejected=false;try{DocumentReadBatch.open(count,invalid);}catch(IOException expected){rejected=true;}check(rejected && invalid.calls.get()==0);++cases;}
        Provider interrupted=new Provider();Thread.currentThread().interrupt();boolean stopped=false;
        try{DocumentReadBatch.open(16,interrupted);}catch(IOException expected){stopped=true;}
        check(stopped && Thread.interrupted() && interrupted.active.get()==0 && interrupted.calls.get()==16 && interrupted.closed.get()==16);++cases;
        // Every successful descriptor has a unique slot; a failed speculative
        // open is represented by -1 and never replaces another slot's result.
        String report="{\"schema\":\"wa2.document-read-batch.v1\",\"cases\":"+cases+",\"files\":100,\"batches\":7,\"maximum_workers\":4,\"maximum_batch\":16,\"partial_failure_isolated\":true,\"interrupted_descriptors_closed\":true,\"all_workers_joined_before_return\":true,\"payload_cached\":false,\"android_device_verified\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
        Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
