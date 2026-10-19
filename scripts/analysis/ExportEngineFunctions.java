// Export local analysis pseudocode for rebuilding complete engine functions.
// These files are review material, not compiled game source or a new backend.
//@category WA2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;

public class ExportEngineFunctions extends GhidraScript {
    private final Gson json=new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
    private void save(Path path,Object value) throws Exception {
        Files.writeString(path,json.toJson(value)+"\n",StandardCharsets.UTF_8);
    }
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("output, image SHA, run ID required");
        Path output=Path.of(args[0]);Files.createDirectories(output);
        Set<Long> priority=new HashSet<>(Arrays.asList(0x43b7a0L,0x43b010L,0x43dff0L,0x442010L,0x442180L,0x475430L,0x475760L,0x4776d0L));
        List<Function> functions=new ArrayList<>();
        FunctionIterator iterator=currentProgram.getFunctionManager().getFunctions(true);
        while(iterator.hasNext()){
            Function f=iterator.next();long at=f.getEntryPoint().getOffset();
            if(!f.isExternal() && ((at>=0x401000L && at<0x4616c0L) || priority.contains(at)))functions.add(f);
        }
        functions.sort(Comparator.comparing((Function f)->!priority.contains(f.getEntryPoint().getOffset()))
            .thenComparingLong(f->f.getEntryPoint().getOffset()));
        Map<String,Object> manifest=new LinkedHashMap<>();
        manifest.put("schema","wa2.engine-source-analysis.v1");manifest.put("input_sha256",args[1]);manifest.put("run_id",args[2]);
        manifest.put("pseudocode_only",true);manifest.put("compiled_into_game",false);manifest.put("correctness_verified",false);
        manifest.put("screen_access",false);manifest.put("functions_selected",functions.size());
        List<Map<String,Object>> rows=new ArrayList<>();manifest.put("functions",rows);manifest.put("complete",false);
        save(output.resolve("manifest.json"),manifest);
        DecompInterface decompiler=new DecompInterface();decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);decompiler.toggleSyntaxTree(false);
        try {
            if(!decompiler.openProgram(currentProgram))throw new IllegalStateException(decompiler.getLastMessage());
            for(Function f:functions){
                monitor.checkCancelled();long at=f.getEntryPoint().getOffset();String stem=String.format("%08x",at);
                Map<String,Object> row=new LinkedHashMap<>();row.put("address",at);row.put("name",f.getName());
                row.put("prototype",f.getSignature().toString());row.put("body_bytes",f.getBody().getNumAddresses());
                row.put("last_address",f.getBody().getMaxAddress().getOffset());
                List<Long> calls=new ArrayList<>();for(Function callee:f.getCalledFunctions(monitor))calls.add(callee.getEntryPoint().getOffset());
                Collections.sort(calls);row.put("calls",calls);
                List<Map<String,Object>> callers=new ArrayList<>();ReferenceIterator references=currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
                while(references.hasNext()){
                    Reference ref=references.next();Map<String,Object> r=new LinkedHashMap<>();
                    r.put("from",ref.getFromAddress().getOffset());r.put("type",ref.getReferenceType().toString());callers.add(r);
                }
                row.put("references",callers);
                DecompileResults result=decompiler.decompileFunction(f,45,monitor);
                if(result.decompileCompleted() && result.getDecompiledFunction()!=null){
                    String c=result.getDecompiledFunction().getC();
                    Files.writeString(output.resolve(stem+".c"),"/* Analysis pseudocode: correctness unverified. */\n"+c,StandardCharsets.UTF_8);
                    row.put("c_file",stem+".c");row.put("c_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(output.resolve(stem+".c")))));
                    row.put("warnings",c.contains("WARNING"));
                } else row.put("error",result.getErrorMessage());
                save(output.resolve(stem+".json"),row);rows.add(row);
                if(rows.size()%25==0 || priority.contains(at)){
                    save(output.resolve("manifest.json"),manifest);
                    println("EXPORTED "+rows.size()+"/"+functions.size()+" "+stem+" "+f.getName());
                }
            }
            manifest.put("complete",true);save(output.resolve("manifest.json"),manifest);
            println("Completed engine analysis export: "+rows.size()+" functions; pseudocode remains unverified.");
        } finally {decompiler.dispose();}
    }
}
