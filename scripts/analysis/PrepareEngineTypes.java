// Supply the already implemented COM calling conventions and known image
// layouts to the local analysis database. No executable bytes are modified.
//@category WA2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;

public class PrepareEngineTypes extends GhidraScript {
    private final CategoryPath category=new CategoryPath("/WA2");
    private DataTypeManager manager;
    private DataType word=UnsignedIntegerDataType.dataType;
    private DataType pointer(DataType type){return manager.resolve(new PointerDataType(type,4,manager),DataTypeConflictHandler.REPLACE_HANDLER);}
    private Structure structure(String name,int size){return (Structure)manager.resolve(new StructureDataType(category,name,size),DataTypeConflictHandler.REPLACE_HANDLER);}
    private void global(long address,DataType type) throws Exception {
        Address start=toAddr(address);clearListing(start,start.add(type.getLength()-1));createData(start,type);
    }
    private void signature(Function function,String convention,DataType result,DataType... types) throws Exception {
        Variable[] parameters=new Variable[types.length];for(int n=0;n<types.length;n++)parameters[n]=new ParameterImpl("arg"+n,types[n],currentProgram);
        function.updateFunction(convention,new ReturnParameterImpl(result,currentProgram),Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,true,SourceType.USER_DEFINED,parameters);
        function.setStackPurgeSize(convention.equals("__stdcall")?types.length*4:0);
    }
    private void intrinsic(long at,String name,boolean integer) throws Exception {
        Function f=getFunctionAt(toAddr(at));if(f==null){disassemble(toAddr(at));f=createFunction(toAddr(at),name);}if(f==null)return;
        f.setName(name,SourceType.USER_DEFINED);
        Variable input=new ParameterImpl("value",Float10DataType.dataType,new VariableStorage(currentProgram,currentProgram.getRegister("ST0")),currentProgram);
        Variable result=new ReturnParameterImpl(integer?IntegerDataType.dataType:Float10DataType.dataType,
            currentProgram.getRegister(integer?"EAX":"ST0"),currentProgram);
        f.updateFunction("__cdecl",result,Function.FunctionUpdateType.CUSTOM_STORAGE,true,SourceType.USER_DEFINED,input);f.setStackPurgeSize(0);
    }
    @Override public void run() throws Exception {
        JsonObject document=JsonParser.parseString(Files.readString(Path.of(getScriptArgs()[0]))).getAsJsonObject();
        manager=currentProgram.getDataTypeManager();Map<String,DataType> objects=new HashMap<>();
        for(String kind:Arrays.asList("d3d","device","texture","surface","vertexbuffer","indexbuffer","vertexshader","pixelshader","declaration","stateblock","buffer")){
            JsonArray methods=document.getAsJsonObject("methods").getAsJsonArray(kind);if(methods==null)continue;
            Structure object=structure("D3D_"+kind,4),table=structure("D3D_"+kind+"_vtable",0);DataType receiver=pointer(object);
            object.replaceAtOffset(0,pointer(table),4,"vtable",null);objects.put(kind,receiver);
            for(JsonElement item:methods){
                JsonObject method=item.getAsJsonObject();String name=method.get("name").getAsString();int count=method.get("arguments").getAsInt();
                FunctionDefinitionDataType definition=new FunctionDefinitionDataType(category,kind+"_"+name,manager);
                ParameterDefinition[] parameters=new ParameterDefinition[count];
                for(int n=0;n<count;n++)parameters[n]=new ParameterDefinitionImpl(n==0?"self":"arg"+n,n==0?receiver:word,null);
                definition.setArguments(parameters);definition.setReturnType(name.equals("GetNPatchMode")?FloatDataType.dataType:word);definition.setCallingConvention("__stdcall");
                DataType function=manager.resolve(definition,DataTypeConflictHandler.REPLACE_HANDLER);table.add(pointer(function),4,name,null);
            }
        }
        global(0xb34434,objects.get("d3d"));global(0xb34438,objects.get("device"));global(0xb3443c,objects.get("surface"));global(0xb34000,objects.get("surface"));global(0xb34008,objects.get("surface"));
        Structure image=structure("ImageSlot",128);
        image.replaceAtOffset(0x5c,ShortDataType.dataType,2,"width",null);image.replaceAtOffset(0x5e,ShortDataType.dataType,2,"height",null);
        image.replaceAtOffset(0x68,ShortDataType.dataType,2,"state",null);image.replaceAtOffset(0x70,pointer(objects.get("texture")),4,"texture_slot",null);
        global(0xa93fb8,new ArrayDataType(image,1024,128,manager));
        Structure sprite=structure("Sprite",256);
        sprite.replaceAtOffset(0x3c,word,4,"blend_mode",null);sprite.replaceAtOffset(0x44,word,4,"flip",null);
        sprite.replaceAtOffset(0x48,ShortDataType.dataType,2,"target_image",null);sprite.replaceAtOffset(0x4a,ShortDataType.dataType,2,"source_image",null);
        sprite.replaceAtOffset(0x52,SignedByteDataType.dataType,1,"palette_index",null);
        sprite.replaceAtOffset(0x53,new ArrayDataType(word,4,4,manager),16,"additive",null);
        sprite.replaceAtOffset(0x63,new ArrayDataType(word,4,4,manager),16,"modulation",null);
        for(int at=0x74;at<=0xc4;at+=4)sprite.replaceAtOffset(at,FloatDataType.dataType,4,String.format("value_%02x",at),null);
        global(0xab3fb8,new ArrayDataType(sprite,2048,256,manager));
        for(JsonElement item:document.getAsJsonArray("imports")){
            JsonObject api=item.getAsJsonObject();String name=api.get("name").getAsString();
            FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(true);
            while(functions.hasNext()){
                Function f=functions.next();if(!f.getName().equals(name))continue;
                DataType[] types=new DataType[api.get("arguments").getAsInt()];Arrays.fill(types,word);
                signature(f,"__stdcall",name.equals("Direct3DCreate9")?objects.get("d3d"):word,types);
            }
            for(Function f:currentProgram.getFunctionManager().getExternalFunctions())if(f.getName().equals(name)){
                DataType[] types=new DataType[api.get("arguments").getAsInt()];Arrays.fill(types,word);
                signature(f,"__stdcall",name.equals("Direct3DCreate9")?objects.get("d3d"):word,types);
            }
        }
        // This frame function is tail-called from another routine. Its body
        // includes the original local switch table before the next function.
        Address frame=toAddr(0x442180);AddressSet body=new AddressSet(frame,toAddr(0x4434bf));
        Function owner=getFunctionContaining(frame);
        if(owner!=null && !owner.getEntryPoint().equals(frame))owner.setBody(new AddressSet(owner.getBody()).subtract(body));
        Function renderer=getFunctionAt(frame);if(renderer==null){disassemble(frame);renderer=currentProgram.getFunctionManager().createFunction("wa2_render_frame",frame,body,SourceType.USER_DEFINED);}
        signature(renderer,"__cdecl",word,word);
        Function draw=getFunctionAt(toAddr(0x43b7a0));if(draw==null){disassemble(toAddr(0x43b7a0));draw=createFunction(toAddr(0x43b7a0),"wa2_draw_sprite");}
        if(draw==null)throw new IllegalStateException("Missing sprite function");
        signature(draw,"__cdecl",VoidDataType.dataType,pointer(sprite),pointer(image));
        intrinsic(0x474f00,"wa2_ftol2_sse",true);intrinsic(0x474990,"wa2_ci_cos",false);intrinsic(0x474d50,"wa2_ci_sin",false);
        println("Applied local COM arities, image/sprite fields and x87 intrinsic storage; source analysis remains unverified.");
    }
}
