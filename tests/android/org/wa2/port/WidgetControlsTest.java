package org.wa2.port;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** Executes production ownership/session helpers on the JVM, without Android UI. */
public final class WidgetControlsTest {
    static int checks;
    static void check(boolean value) { if (!value) throw new AssertionError("Widget ownership check " + checks); ++checks; }
    static WidgetControls.Definition definition(int token, int id, String type, int style, int x) {
        return new WidgetControls.Definition(token, id, type, style, new int[]{x, 2, 40, 12});
    }
    interface Attempt { void run() throws Exception; }
    static void rejects(Attempt attempt) throws Exception {
        boolean failed=false;try { attempt.run(); } catch(IllegalArgumentException error) { failed=true; }check(failed);
    }
    public static void main(String[] args) throws Exception {
        WidgetControls<Object> registry=new WidgetControls<>();int[] created={0};
        WidgetControls.Factory<Object> factory=value -> { ++created[0];return new Object(); };
        WidgetControls.Definition first=definition(0xb1000010,101,"#128",0x50000007,1);
        WidgetControls.Definition second=definition(0xb1000020,102,"msctls_trackbar32",0x50000000,4);
        List<WidgetControls.Definition> original=Arrays.asList(first,second);
        WidgetControls.Change<Object> initial=registry.synchronize(original,factory);
        check(initial.layoutChanged && initial.ordered.size()==2 && created[0]==2);
        Object firstView=registry.get(first.token),secondView=registry.get(second.token);
        check(firstView!=null && secondView!=null);
        WidgetControls.Change<Object> unchanged=registry.synchronize(original,factory);
        check(!unchanged.layoutChanged && unchanged.removed.isEmpty() && created[0]==2);
        check(registry.get(first.token)==firstView && registry.get(second.token)==secondView);
        // Complete updates must populate missing tokens before property lookup.
        WidgetControls.Definition third=definition(0xb1000030,-1,"#130",0x50000000,8);
        check(registry.get(third.token)==null);
        WidgetControls.Change<Object> added=registry.synchronize(Arrays.asList(first,second,third),factory);
        check(added.layoutChanged && registry.get(third.token)!=null && registry.get(first.token)==firstView && created[0]==3);
        WidgetControls.Change<Object> reordered=registry.synchronize(Arrays.asList(third,second,first),factory);
        check(reordered.layoutChanged && created[0]==3 && reordered.ordered.get(0).definition.token==third.token);
        WidgetControls.Definition moved=definition(second.token,second.id,second.type,second.style,99);
        check(registry.synchronize(Arrays.asList(first,moved,third),factory).layoutChanged && registry.get(second.token)==secondView && created[0]==3);
        WidgetControls.Definition changedType=definition(second.token,second.id,"#129",second.style,99);
        WidgetControls.Change<Object> replaced=registry.synchronize(Arrays.asList(first,changedType),factory);
        check(replaced.layoutChanged && replaced.removed.size()==2 && registry.get(third.token)==null && registry.get(second.token)!=secondView);
        Object replacement=registry.get(second.token);int before=created[0];
        rejects(() -> registry.synchronize(Arrays.asList(first,first),factory));
        check(created[0]==before && registry.get(second.token)==replacement && registry.get(first.token)==firstView);
        rejects(() -> registry.synchronize(Collections.nCopies(4097,first),factory));
        rejects(() -> new WidgetControls.Definition(0,1,"#128",0,new int[4]));
        WidgetControls.Definition next=definition(17,1,"#128",0,0),last=definition(18,2,"#128",0,0);
        boolean failed=false;try {registry.synchronize(Arrays.asList(next,last),value -> {if(value.token==18)throw new Exception("fixture factory failure");return new Object();});}catch(Exception error){failed=true;}
        check(failed && registry.get(next.token)==null && registry.get(second.token)==replacement && registry.get(first.token)==firstView);
        rejects(() -> registry.synchronize(Arrays.asList(next),value -> null));
        check(registry.get(first.token)==firstView);
        int cycles=0;
        for(int n=0;n<64;++n){registry.clear();check(registry.get(first.token)==null);registry.synchronize(original,factory);
            Object view=registry.get(first.token);int count=created[0];registry.synchronize(original,factory);check(registry.get(first.token)==view && created[0]==count && registry.entries().size()==2);++cycles;}
        WidgetSession<Object> session=new WidgetSession<>();Object a=new Object(),b=new Object();int sessionCases=0;
        check(!session.current(session.capture()));++sessionCases;
        check(session.attach(a));WidgetSession.Ticket<Object> old=session.capture();check(session.current(old));++sessionCases;
        check(!session.attach(a) && session.current(old));++sessionCases;
        check(session.attach(b) && !session.current(old));WidgetSession.Ticket<Object> current=session.capture();++sessionCases;
        check(!session.detach(a) && session.current(current));++sessionCases;
        check(session.detach(b) && !session.current(current));++sessionCases;
        check(session.attach(b) && !session.current(current));WidgetSession.Ticket<Object> reused=session.capture();check(session.current(reused));++sessionCases;
        int[] work={0};Runnable stale=() -> {if(session.current(current))++work[0];};Runnable fresh=() -> {if(session.current(reused))++work[0];};stale.run();fresh.run();check(work[0]==1);++sessionCases;
        check(session.detach(b) && !session.current(reused));++sessionCases;
        int cachedControls=0,cachedDialogs=0;
        if(args.length>1){List<WidgetControls.Definition> group=new ArrayList<>();
            for(String line:Files.readAllLines(Path.of(args[1]))){if(line.isEmpty()){
                    if(!group.isEmpty()){cachedControls+=checkCached(group);++cachedDialogs;group.clear();}continue;}
                String[] fields=line.split("\t");check(fields.length==8);int[] bounds=new int[4];for(int n=0;n<4;++n)bounds[n]=Integer.parseInt(fields[4+n]);
                group.add(new WidgetControls.Definition((int)Long.parseLong(fields[0]),(int)Long.parseLong(fields[1]),fields[2],(int)Long.parseLong(fields[3]),bounds));}
            if(!group.isEmpty()){cachedControls+=checkCached(group);++cachedDialogs;}
            check(cachedControls==53 && cachedDialogs==7);
        }
        Files.writeString(Path.of(args[0]),"{\"schema\":\"wa2.widget-ownership.v1\",\"checks\":"+checks+",\"reopen_cycles\":"+cycles+",\"activity_session_cases\":"+sessionCases
            +",\"cached_controls\":"+cachedControls+",\"cached_dialogs\":"+cachedDialogs+",\"missing_entries_created_before_update\":true,\"unchanged_views_reused\":true,\"invalid_snapshot_atomic\":true,\"stale_sessions_rejected\":true,\"native_ui_created\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n");
        System.out.println("PASS widget ownership: "+checks+" checks, "+cycles+" reopen cycles and "+sessionCases+" activity session cases; no Android runtime/UI.");
    }
    static int checkCached(List<WidgetControls.Definition> values) throws Exception {
        WidgetControls<Object> registry=new WidgetControls<>();int[] made={0};WidgetControls.Factory<Object> factory=value->{++made[0];return new Object();};
        registry.synchronize(values,factory);check(made[0]==values.size());List<WidgetControls.Entry<Object>> old=registry.entries();
        check(!registry.synchronize(values,factory).layoutChanged && made[0]==values.size());
        for(WidgetControls.Entry<Object> entry:old)check(registry.get(entry.definition.token)==entry.view);
        List<WidgetControls.Definition> changed=new ArrayList<>();
        for(WidgetControls.Definition value:values)changed.add(new WidgetControls.Definition(value.token+0x10000,value.id,value.type,value.style,value.bounds));
        WidgetControls.Change<Object> update=registry.synchronize(changed,factory);check(update.removed.size()==values.size());
        for(WidgetControls.Definition value:values)check(registry.get(value.token)==null);
        for(WidgetControls.Definition value:changed)check(registry.get(value.token)!=null);
        return values.size();
    }
}
