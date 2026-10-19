"""Compare real CRT memmove bytes with the generated native instruction bodies.

The oracle is test-only Unicorn. It is not linked into the game or used as a
runtime fallback. Every byte of the stack/source/destination arena is compared.
"""
from pathlib import Path
import argparse,ctypes as ct,hashlib,json,os,random,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
import unicorn as uc
from unicorn import x86_const as x86
STACK=0x07000000;SIZE=65536;ESP=STACK+0xf000;STOP=0x0badf00d;ENTRY=0x475760;STATUS=0x8d5
REGS=[x86.UC_X86_REG_EAX,x86.UC_X86_REG_ECX,x86.UC_X86_REG_EDX,x86.UC_X86_REG_EBX,x86.UC_X86_REG_ESP,x86.UC_X86_REG_EBP,x86.UC_X86_REG_ESI,x86.UC_X86_REG_EDI]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--vectors',type=int,default=1024);args=parser.parse_args()
    out=ROOT/'build/memory-tests';out.mkdir(parents=True,exist_ok=True)
    source='\n'.join('#include "'+(ROOT/f'generated/wa2-win64/code_page_{n}.cpp').as_posix()+'"' for n in [1141,1150])+r'''
#include <string>
#include <optional>
static thread_local std::string error;
extern "C" __declspec(dllexport) const char* wr_error(){return error.c_str();}
extern "C" __declspec(dllexport) int wr_execute(wr::U32* state,unsigned char* arena,const unsigned char* image,std::size_t size,wr::U32 base,unsigned scoped){
 try{wr::Cpu cpu;for(unsigned n=0;n<8;++n)cpu.r[n]=state[n];cpu.eip=state[8];cpu.flags=state[9];cpu.defined_flags=state[10];
 wr::Memory memory;memory.map(base,size,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);memory.initialize(base,std::span(image,size));
 memory.map(0x07000000,65536,wr::Memory::Read|wr::Memory::Write);memory.initialize(0x07000000,std::span(arena,65536));
 std::optional<wr::Memory::ExecutionScope> scope;if(scoped)scope.emplace(memory);
 std::uint64_t budget=1000000;while(cpu.eip!=0x0badf00d){wr::tick(budget,cpu.eip);bool found=false;
 if((cpu.eip>>12)==1141)found=code_page_1141(cpu,memory,budget);else if((cpu.eip>>12)==1150)found=code_page_1150(cpu,memory,budget);
 if(!found)throw wr::GuestFault(wr::FaultKind::untranslated,cpu.eip,"missing memory-test instruction");}
 for(unsigned n=0;n<8;++n)state[n]=cpu.r[n];state[8]=cpu.eip;state[9]=cpu.flags;state[10]=cpu.defined_flags;
 memory.copy_out(0x07000000,std::span(arena,65536));return 0;
 }catch(const std::exception& e){error=e.what();return 1;}
}
'''
    wrapper=out/'wrapper.cpp';wrapper.write_text(source);library=out/'memory.dll'
    command=[str(ROOT/'scripts/zig-cxx.cmd'),'-std=c++20','-O2','-shared','-I',str(ROOT/'tools/WindowsRecompTool-main/include'),str(wrapper),str(ROOT/'build/wa2-win64/winrecomp/libwinrecomp_native.a'),'-o',str(library)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=120)
    (ROOT/'logs/memory-compile.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    dll=ct.CDLL(str(library));dll.wr_execute.argtypes=[ct.POINTER(ct.c_uint32),ct.POINTER(ct.c_ubyte),ct.POINTER(ct.c_ubyte),ct.c_size_t,ct.c_uint32,ct.c_uint];dll.wr_execute.restype=ct.c_int;dll.wr_error.restype=ct.c_char_p
    pe=pefile.PE(str(ROOT/'WA2_chs.exe'));base=pe.OPTIONAL_HEADER.ImageBase;size=pe.OPTIONAL_HEADER.SizeOfImage
    mapped=bytearray(pe.get_memory_mapped_image());mapped+=bytes(size-len(mapped))
    machine=uc.Uc(uc.UC_ARCH_X86,uc.UC_MODE_32);machine.mem_map(base,size);machine.mem_map(STACK,SIZE);initial=machine.context_save()
    sizes=[0,1,2,3,4,7,8,9,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,511,512,513,1023,4095,8192]
    rng=random.Random(0x475824);started=time.perf_counter();count=0
    for fast in [0,1]:
        struct.pack_into('<I',mapped,0xbc8310-base,fast);machine.mem_write(base,bytes(mapped));native_image=(ct.c_ubyte*size).from_buffer_copy(mapped)
        for k in range(args.vectors):
            source_offset=0x1000+(k//16)%16;destination_offset=0x9000+k%16;mode=(k//256)%4
            if mode==1:destination_offset=0x1010+k%16
            if mode==2:source_offset+=16;destination_offset=0x1000+k%16
            if mode==3:destination_offset=source_offset
            length=sizes[k%len(sizes)];arena=bytearray(rng.randbytes(SIZE));struct.pack_into('<4I',arena,ESP-STACK,STOP,STACK+destination_offset,STACK+source_offset,length)
            regs=[rng.getrandbits(32) for _ in range(8)];regs[4]=ESP;flags=0x202|(rng.getrandbits(12)&STATUS)
            machine.context_restore(initial);machine.mem_write(STACK,bytes(arena))
            for register,value in zip(REGS,regs):machine.reg_write(register,value)
            machine.reg_write(x86.UC_X86_REG_EFLAGS,flags);machine.emu_start(ENTRY,STOP,count=1000000)
            expected=[machine.reg_read(register) for register in REGS]+[machine.reg_read(x86.UC_X86_REG_EIP),machine.reg_read(x86.UC_X86_REG_EFLAGS)]
            expected_arena=bytes(machine.mem_read(STACK,SIZE))
            for scoped in [0,1]:
                state=(ct.c_uint32*11)(*regs,ENTRY,flags,STATUS);native_arena=(ct.c_ubyte*SIZE).from_buffer_copy(arena)
                if dll.wr_execute(state,native_arena,native_image,size,base,scoped):raise RuntimeError(f'feature={fast} scoped={scoped} vector={k}: {dll.wr_error().decode()}')
                actual=list(state)
                if actual[:9]!=expected[:9] or ((actual[9]^expected[9])&(actual[10]|0x400)&(STATUS|0x400)):raise RuntimeError(f'CPU mismatch: feature={fast} scoped={scoped} vector={k}\nactual={actual}\nexpected={expected}')
                if bytes(native_arena)!=expected_arena:raise RuntimeError(f'Memory mismatch: feature={fast} scoped={scoped} vector={k}, length={length}, source={source_offset:#x}, destination={destination_offset:#x}')
                count+=1
    record={'schema':'wa2.memory-conformance.v1','original_sha256':hashlib.sha256((ROOT/'WA2_chs.exe').read_bytes()).hexdigest(),'entry':hex(ENTRY),'total_vectors':count,'oracle':'Unicorn IA-32 (tests only)','profiles':['CRT scalar alignment/unroll/tail tables','CRT SSE fast-copy branch'],'locking_profiles':['individual memory locks','translated instruction execution scope'],'full_64KiB_arena_compared':True,'elapsed_seconds':round(time.perf_counter()-started,3)}
    (out/'report.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
if __name__=='__main__':main()
