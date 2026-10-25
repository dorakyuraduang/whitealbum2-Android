// Read existing analysis records only. No import, disassembly or decompilation.
//@category WA2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.*;
import com.google.gson.GsonBuilder;

public class ExportCachedListing extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=1)throw new IllegalArgumentException("One cached JSON output path is required");
        Map<String,Object> report=new LinkedHashMap<>();
        report.put("schema","wa2.existing-analysis-listing.v1");
        report.put("program_name",currentProgram.getName());
        report.put("source_executable_opened",false);
        report.put("new_analysis_performed",false);
        report.put("decompiler_used",false);
        report.put("program_modified",false);
        List<Map<String,Object>> functions=new ArrayList<>(),instructions=new ArrayList<>();
        var funcs=currentProgram.getFunctionManager().getFunctions(true);
        while(funcs.hasNext()){
            monitor.checkCancelled();Function f=funcs.next();if(f.isExternal())continue;
            Map<String,Object> row=new LinkedHashMap<>();
            row.put("address",f.getEntryPoint().getOffset());row.put("name",f.getName());
            row.put("body_bytes",f.getBody().getNumAddresses());
            List<List<Long>> ranges=new ArrayList<>();
            for(AddressRange range:f.getBody().getAddressRanges())ranges.add(Arrays.asList(range.getMinAddress().getOffset(),range.getMaxAddress().getOffset()));
            row.put("ranges",ranges);List<Map<String,Object>> references=new ArrayList<>();
            var refs=currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            while(refs.hasNext()){
                Reference ref=refs.next();Map<String,Object> item=new LinkedHashMap<>();
                item.put("from",ref.getFromAddress().getOffset());item.put("type",ref.getReferenceType().toString());references.add(item);
            }
            row.put("references",references);functions.add(row);
        }
        var listing=currentProgram.getListing().getInstructions(true);
        while(listing.hasNext()){
            monitor.checkCancelled();Instruction inst=listing.next();
            Map<String,Object> row=new LinkedHashMap<>();row.put("address",inst.getAddress().getOffset());
            row.put("size",inst.getLength());row.put("bytes",HexFormat.of().formatHex(inst.getBytes()));
            row.put("text",inst.toString());row.put("mnemonic",inst.getMnemonicString());
            Address fall=inst.getFallThrough();row.put("fallthrough",fall==null?null:fall.getOffset());
            row.put("flow_type",inst.getFlowType().toString());
            List<Long> flows=new ArrayList<>();for(Address at:inst.getFlows())flows.add(at.getOffset());row.put("flows",flows);
            List<String> operands=new ArrayList<>();
            for(int n=0;n<inst.getNumOperands();++n)operands.add(inst.getDefaultOperandRepresentation(n));row.put("operand_text",operands);
            List<Long> codeReferences=new ArrayList<>();
            for(Reference ref:inst.getReferencesFrom()){
                long target=ref.getToAddress().getOffset();
                if(ref.getReferenceType().isData() && target>=0x401000L && target<0x4a1000L)codeReferences.add(target);
            }
            if(!codeReferences.isEmpty())row.put("code_references",codeReferences);
            instructions.add(row);
        }
        report.put("functions",functions);report.put("instructions",instructions);
        Path destination=Path.of(args[0]);Files.createDirectories(destination.getParent());
        Files.writeString(destination,new GsonBuilder().disableHtmlEscaping().create().toJson(report)+"\n",StandardCharsets.UTF_8);
        println("READ EXISTING CACHE: "+functions.size()+" functions, "+instructions.size()+" instruction records; no new analysis.");
    }
}
