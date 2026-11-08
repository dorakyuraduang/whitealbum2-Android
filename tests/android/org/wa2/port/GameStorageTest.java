package org.wa2.port;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;

public final class GameStorageTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    interface Attempt{void run()throws IOException;}
    static void rejects(Attempt action)throws IOException{try{action.run();}catch(IOException expected){return;}throw new AssertionError("Expected failure");}
    static final class Memory implements GameFilePublication.Backend<String> {
        Map<String,byte[]> files=new HashMap<>();int operations,failAt=-1,secondFailure=-1;
        void step()throws IOException{int current=operations++;if(current==failAt || current==secondFailure)throw new IOException("injected provider failure "+current);}
        public String find(String name)throws IOException{step();return files.containsKey(name)?name:null;}
        public String create(String name)throws IOException{step();if(files.containsKey(name))throw new IOException("duplicate");files.put(name,new byte[0]);return name;}
        public String rename(String file,String name)throws IOException{step();if(!files.containsKey(file)||files.containsKey(name))throw new IOException("bad rename");files.put(name,files.remove(file));return name;}
        public void remove(String file)throws IOException{step();if(files.remove(file)==null)throw new IOException("missing remove");}
        public void write(String file,ByteBuffer bytes)throws IOException{step();byte[] value=new byte[bytes.remaining()];bytes.get(value);files.put(file,value);}
    }
    public static void main(String[] args)throws Exception{
        int cases=0,guards=0;
        for(int slot=0;slot<1000;++slot){String name=String.format(java.util.Locale.ROOT,"slot-%03d.wa2sav",slot);check(GameStoragePath.slot(name) && GameStoragePath.valid("native-saves/"+name,true));++cases;}
        for(String name:new String[]{"Sys.sav","native-settings.wa2cfg","native-launcher.wa2cfg","native-saves/slot-999.wa2sav"}){check(GameStoragePath.valid(name,true));++cases;}
        for(String prefix:LegacySavePath.DIRECTORIES){check(GameStoragePath.valid(prefix+"Sys.sav",false) && GameStoragePath.valid(prefix+"Sys.sav",true)==prefix.isEmpty());++cases;}
        for(String name:new String[]{"native-test/Sys.sav","native-test/native-settings.wa2cfg","native-test/native-saves/slot-999.wa2sav"}){check(GameStoragePath.valid(name,true));++cases;}
        for(String name:new String[]{"native-test/native-test/Sys.sav","native-test/Documents/Leaf/WHITE ALBUM2/Sys.sav","native-test/../Sys.sav"}){check(!GameStoragePath.valid(name,false) && !GameStoragePath.valid(name,true));++guards;}
        for(String name:new String[]{null,"","..","../Sys.sav","native-saves/../Sys.sav","/Sys.sav","C:/Sys.sav","native-saves\\slot-000.wa2sav","native-saves/slot-1000.wa2sav","native-saves/slot-abc.wa2sav","native-saves/slot-000.wa2sav.tmp","grp.pak","SYSTEM.ini","save_00.sav","native-settings.wa2cfg/evil"}){check(!GameStoragePath.valid(name,false) && !GameStoragePath.valid(name,true));rejects(()->GameStoragePath.parts(name,true));++guards;}
        String name="slot-012.wa2sav";byte[] old={1,2,3},next={4,5,6};Memory successful=new Memory();successful.files.put(name,old);ByteBuffer buffer=ByteBuffer.wrap(next);GameFilePublication.replace(successful,name,buffer);check(successful.files.size()==1 && successful.files.get(name)[0]==4 && buffer.position()==0);int operations=successful.operations;++cases;
        for(int failed=0;failed<operations;++failed){Memory memory=new Memory();memory.files.put(name,old);memory.failAt=failed;
            boolean threw=false;try{GameFilePublication.replace(memory,name,ByteBuffer.wrap(next));}catch(IOException expected){threw=true;}
            memory.failAt=-1;String recovered=GameFilePublication.recover(memory,name);check(name.equals(recovered) && memory.files.size()==1);check(memory.files.get(name)[0]==(threw?1:4));++guards;++cases;
        }
        for(int state=0;state<5;++state){Memory memory=new Memory();if(state==0||state==2||state==4)memory.files.put(name,old);if(state==1||state==2)memory.files.put(name+GameFilePublication.BACKUP,old);if(state!=4)memory.files.put(name+GameFilePublication.STAGE,next);String recovered=GameFilePublication.recover(memory,name);check((recovered==null)==(state==3) && memory.files.size()==(state==3?0:1));if(recovered!=null)check(memory.files.get(name)[0]==1);++cases;}
        Memory rollback=new Memory();rollback.files.put(name,old);rollback.failAt=6;rollback.secondFailure=7;rejects(()->GameFilePublication.replace(rollback,name,ByteBuffer.wrap(next)));check(rollback.files.containsKey(name+GameFilePublication.BACKUP));rollback.failAt=rollback.secondFailure=-1;check(GameFilePublication.recover(rollback,name).equals(name) && rollback.files.get(name)[0]==1 && rollback.files.size()==1);++guards;++cases;
        Memory fresh=new Memory();GameFilePublication.replace(fresh,name,ByteBuffer.wrap(next));check(fresh.files.size()==1 && fresh.files.get(name)[0]==4);++cases;
        String report="{\"schema\":\"wa2.android-game-storage.v1\",\"cases\":"+cases+",\"guards\":"+guards+",\"fault_injected_publication\":true,\"interrupted_write_recovery\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";Files.write(Paths.get(args[0]),report.getBytes("UTF-8"));
    }
}
