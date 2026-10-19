"""Generate register-resident x64 basic blocks, with build-time decoding only.

The original single-instruction functions remain the correctness oracle. Native
blocks use hardware integer flags and x87 state, and translate checked memory
operands through the locked native page view. No C++ helper runs inside a block.
"""
from pathlib import Path
from collections import Counter
import hashlib
import json
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/python'))
import capstone as cs
import pefile
from generate_static_blocks import PAGES as HOT_PAGES,SPECIALIZED_ENTRIES,CASE
from retired_modules import RANGES as RETIRED_RANGES,contains as retired_contains,MANIFEST as RETIRED_MANIFEST
PAGES=tuple(sorted(int(path.stem.split('_')[-1]) for path in (ROOT/'generated/wa2-win64').glob('code_page_*.cpp')))
LINEAR_MODE=False
MAX_BLOCK_INSTRUCTIONS=32
MAX_TRACE_INSTRUCTIONS=256
NATIVE_FUNCTIONS=json.loads((ROOT/'port/native_functions.json').read_text())
NATIVE_RANGES=tuple((f['start'],f['end']) for f in NATIVE_FUNCTIONS['functions'])
NATIVE_ENTRIES={start for start,end in NATIVE_RANGES}


def forward_successors(nodes,blocks,parts,start):
    ins=nodes[blocks[start][-1]][0];next_pc=ins.address+ins.size
    if re.fullmatch('j'+CONDITION,ins.mnemonic):return (ins.operands[0].imm,next_pc)
    if ins.mnemonic in ('call','jmp'):
        return (ins.operands[0].imm,) if ins.operands[0].type==cs.x86.X86_OP_IMM else ()
    if ins.mnemonic in ('ret','fldcw') or parts[start]['repeat']:return ()
    return (next_pc,)


def region_defined_states(nodes,blocks,parts,root,members):
    # The context word remains the region's input until an exit/fault. Only
    # merge paths with the exact same symbolic transform of that input.
    incoming={root:(STATUS,0)};outgoing={}
    for start in sorted(members):
        if start not in incoming:return None
        keep,bits=incoming[start];mask,value=parts[start]['defined_transform']
        state=(keep&mask,(bits&mask)|value);outgoing[start]=state
        for child in forward_successors(nodes,blocks,parts,start):
            if child in members and child>start:
                if child in incoming and incoming[child]!=state:return None
                incoming[child]=state
    return incoming,outgoing


def defined_metadata(keep,bits):
    if not keep:return [f'    mov dword ptr [r8+40], {bits}']
    if keep==STATUS and not bits:return []
    return ['    pushfq',*([f'    and dword ptr [r8+40], {keep}'] if keep!=STATUS else []),
        *([f'    or dword ptr [r8+40], {bits}'] if bits else []),'    popfq']


def forward_regions(nodes,blocks,parts):
    """Reserve a bounded DAG path once; backward/foreign edges keep guards."""
    mandatory=set(SPECIALIZED_ENTRIES)
    for ins,_ in nodes.values():
        if ins.mnemonic in ('call','jmp') or re.fullmatch('j'+CONDITION,ins.mnemonic):
            if ins.operands and ins.operands[0].type==cs.x86.X86_OP_IMM:
                target=ins.operands[0].imm
                if ins.mnemonic=='call' or target<=ins.address:mandatory.add(target)
    def successors(start):return forward_successors(nodes,blocks,parts,start)
    regions={};claimed=set()
    for root in sorted(blocks):
        if (root in claimed and root not in mandatory) or not parts[root]['eligible']:continue
        members={root};pending=[root]
        while pending and len(members)<64:
            start=pending.pop()
            for child in successors(start):
                if start<child<root+1024 and child in parts and parts[child]['eligible'] and child not in members:
                    members.add(child);pending.append(child)
        def length():
            longest={}
            for start in sorted(members,reverse=True):
                longest[start]=len(blocks[start])+max((longest[child] for child in successors(start) if child in members and child>start),default=0)
            return longest[root]
        while len(members)>1 and length()>MAX_TRACE_INSTRUCTIONS:members.remove(max(members))
        if len(members)>2:
            regions[root]=(members,length());claimed.update(members)
    return regions

STATUS,AF,CF,OF=0x8d5,0x10,1,0x800
REGS={name:name for name in ('eax','ax','al','ah','ecx','cx','cl','ch','edx','dx','dl','dh',
    'ebx','bx','bl','bh','ebp','bp','esi','si','edi','di')}
REGS.update(esp='r12d',sp='r12w')
REGS.update({f'xmm{i}':f'xmm{i}' for i in range(8)})
BASE64=dict(eax='rax',ecx='rcx',edx='rdx',ebx='rbx',esp='r12',ebp='rbp',esi='rsi',edi='rdi')
REG_FAMILY={name:root for root,names in {
    'eax':('eax','ax','al','ah'),'ecx':('ecx','cx','cl','ch'),'edx':('edx','dx','dl','dh'),
    'ebx':('ebx','bx','bl','bh'),'esp':('esp','sp'),'ebp':('ebp','bp'),'esi':('esi','si'),'edi':('edi','di')}.items() for name in names}
GP={'mov','movzx','movsx','lea','add','sub','adc','sbb','cmp','neg','and','or','xor','test',
    'inc','dec','not','imul','mul','bswap','cdq','cwd','cwde','nop','clc','stc','cmc','xchg'}
SHIFTS={'shl','sal','shr','sar','rol','ror'}
CONDITION=r'(?:o|no|b|ae|e|ne|be|a|s|ns|p|np|l|ge|le|g)'
REG_FP=re.compile(r'wr::x87_execute\(s,wr_x87_\d+_r\d+,nullptr,(\d+),false,false\);')
MEM_FP=re.compile(r'wr::x87_memory\(s,m,wr_x87_\d+_m\d+,(.+),(\d+),(true|false),(\d+),false\);')


def address(ins,operand):
    memory=operand.mem
    parts=[]
    if memory.base:
        parts.append(BASE64[ins.reg_name(memory.base)])
    if memory.index:
        parts.append(BASE64[ins.reg_name(memory.index)]+f'*{memory.scale}')
    displacement=memory.disp
    if not parts:
        return f'    mov r10d, {displacement&0xffffffff}'
    expression='+'.join(parts)
    if displacement:
        if displacement>0x7fffffff:displacement-=0x100000000
        expression+=f'{displacement:+d}'
    return f'    lea r10d, [{expression}]'


def render(ins,operand,memory=False,memory_size=None):
    if operand.type==cs.x86.X86_OP_REG:return REGS[ins.reg_name(operand.reg)]
    if operand.type==cs.x86.X86_OP_IMM:return str(operand.imm)
    assert memory and operand.type==cs.x86.X86_OP_MEM
    return {1:'byte',2:'word',4:'dword',8:'qword',10:'tbyte',16:'xmmword'}[memory_size or operand.size]+' ptr [r10]'


class ReturnHint:
    # F3 C3 is a single RET with a branch-prediction hint, not a repeat loop.
    # Preserve its original size/bytes and code proof while lowering RET.
    mnemonic='ret'
    def __init__(self,instruction):self.instruction=instruction
    def __getattr__(self,name):return getattr(self.instruction,name)


def select(ins,body):
    mnemonic=ins.mnemonic
    string=re.fullmatch(r'wr::string_op\(s,m,([01]),(8|16|32),32,1,0,budget\);',body)
    if string and ins.mnemonic.startswith(('rep stos','rep movs')):
        return dict(fp=False,rep_stos=True,rep_move=string[1]=='0',memory=None,write=True,access=2)
    memory=[operand for operand in ins.operands if operand.type==cs.x86.X86_OP_MEM]
    if len(memory)>1:return None
    if any(operand.type==cs.x86.X86_OP_REG and ins.reg_name(operand.reg) not in REGS
           for operand in ins.operands) and not 0xd8<=ins.bytes[0]<=0xdf:return None
    if memory:
        operand=memory[0]
        if ins.addr_size!=4 or operand.mem.segment or operand.size not in (1,2,4,8,10,16):return None
        if any(register and ins.reg_name(register) not in BASE64 for register in (operand.mem.base,operand.mem.index)):return None
        if any(operand.type==cs.x86.X86_OP_REG and ins.reg_name(operand.reg) in ('ah','bh','ch','dh') for operand in ins.operands):return None
    if 0xd8<=ins.bytes[0]<=0xdf:
        if mnemonic=='fnstcw' and len(memory)==1 and ',s.fp.get16(0),16);' in body:
            return dict(fp=False,control_store=True,memory=memory[0],write=True,access=2)
        if mnemonic=='fldcw' and len(memory)==1 and body.endswith(',true);'):
            return dict(fp=True,control_load=True,memory=memory[0],write=False,access=1)
        if bytes(ins.bytes)==b'\xdf\xe0' and body=='wr::set_reg(s,0,s.fp.get16(2),16,0);':
            return dict(fp=True,fp_status=True,fop=0,memory=None,write=False,access=0)
        register=REG_FP.fullmatch(body)
        operand=MEM_FP.fullmatch(body)
        if register:return dict(fp=True,fop=int(register[1]),memory=None,write=False,access=0)
        if operand and len(memory)==1:return dict(fp=True,fop=int(operand[4]),memory=memory[0],
            write=operand[3]=='true',access=2 if operand[3]=='true' else 1)
        return None
    if mnemonic=='lea':
        return dict(fp=False,memory=None,write=False,access=0)
    if mnemonic=='idiv':
        if len(ins.operands)!=1 or ins.operands[0].type!=cs.x86.X86_OP_REG or ins.operands[0].size!=4 or ins.reg_name(ins.operands[0].reg)=='eax':return None
        if not body.startswith('wr::divide(s,') or ',32,true);' not in body:return None
        return dict(fp=False,memory=None,write=False,access=0,signed_divide=True)
    if mnemonic in ('push','pop'):
        if len(ins.operands)!=1 or memory:return None
        operand=ins.operands[0]
        if operand.size not in (2,4) or (mnemonic=='pop' and operand.type!=cs.x86.X86_OP_REG):return None
        return dict(fp=False,memory=None,write=mnemonic=='push',access=2 if mnemonic=='push' else 1)
    if mnemonic in ('call','jmp'):
        if 0x66 in ins.prefix:return None
        if len(ins.operands)!=1 or ins.operands[0].size!=4:return None
        return dict(fp=False,memory=memory[0] if memory else None,write=mnemonic=='call',access=1 if memory else (2 if mnemonic=='call' else 0))
    if mnemonic=='ret':return None if 0x66 in ins.prefix else dict(fp=False,memory=None,write=False,access=1)
    if mnemonic=='leave':return None if 0x66 in ins.prefix else dict(fp=False,memory=None,write=False,access=1)
    if mnemonic in ('sahf','fwait'):return dict(fp=False,memory=None,write=False,access=0)
    if mnemonic=='stmxcsr' and len(memory)==1 and 's.fp.get32(24)' in body:
        return dict(fp=True,mxcsr_store=True,memory=memory[0],write=True,access=2)
    if any(operand.type==cs.x86.X86_OP_REG and ins.reg_name(operand.reg).startswith('xmm') for operand in ins.operands) and any(
        helper in body for helper in ('wr::sse_execute(s,','wr::sse_memory(s,m,','wr::sse_sd_to_i32(s,','wr::sse_i32_to_sd(s,','wr::xmm_')):
        operand=memory[0] if memory else None
        access=operand.access if operand else 0
        if operand and access not in (1,2):return None
        alignment=0;memory_size=operand.size if operand else 0
        match=re.search(r',(\d+),(true|false),(\d+),(true|false)\);$',body)
        if operand and 'wr::sse_memory(s,m,' in body:
            if not match:return None
            alignment=int(match[3]);memory_size=int(match[1])
        elif operand and any(helper in body for helper in ('xmm_shuffle_memory','xmm_shuffle_pd_memory')):alignment=16
        return dict(fp=True,sse=True,alignment=alignment,normalize_control='wr::sse_' in body,
            sse_flags=mnemonic in ('comisd','ucomisd','comiss','ucomiss'),
            memory=operand,memory_size=memory_size,write=bool(access&2),access=access)
    if mnemonic in ('cvttsd2si','cvtsd2si') and len(memory)==1:
        if memory[0].size!=8 or ins.operands[0].type!=cs.x86.X86_OP_REG or ins.operands[0].size!=4:return None
        if 'wr::sse_sd_memory_to_i32(s,m,' not in body:return None
        return dict(fp=True,sse=True,memory=memory[0],write=False,access=1)
    if re.fullmatch('j'+CONDITION,mnemonic):return None if 0x66 in ins.prefix else dict(fp=False,memory=None,write=False,access=0)
    if mnemonic in SHIFTS:
        if len(ins.operands)!=2 or ins.operands[1].type!=cs.x86.X86_OP_IMM:return None
    elif mnemonic not in GP and not re.fullmatch('(set|cmov)'+CONDITION,mnemonic):return None
    # REX register addressing cannot encode the legacy high-byte registers.
    names=[ins.reg_name(operand.reg) for operand in ins.operands if operand.type==cs.x86.X86_OP_REG]
    if 'esp' in names and any(name in ('ah','bh','ch','dh') for name in names):return None
    operand=memory[0] if memory else None
    access=operand.access if operand else 0
    if operand and not access:
        # Capstone 5 omits operand access metadata on memory TEST and some
        # other integer forms. Infer access from their fixed x86 semantics.
        if mnemonic in ('test','cmp','movzx','movsx','mul','imul'):access=1
        elif mnemonic=='mov':access=2 if ins.operands[0].type==cs.x86.X86_OP_MEM else 1
        elif mnemonic=='xchg':access=3
        elif mnemonic in ('add','sub','adc','sbb','and','or','xor','inc','dec','neg','not') or mnemonic in SHIFTS:
            access=3 if ins.operands[0].type==cs.x86.X86_OP_MEM else 1
    if operand and access not in (1,2,3):return None
    return dict(fp=False,memory=operand,write=bool(access&2),access=access)


def memory_check(ins,operand,size,access,fault):
    ea=address(ins,operand) if operand else ('    mov r10d, ebp' if ins.mnemonic=='leave' else '    lea r10d, [r12'+('-'+str(size) if access==2 else '')+']')
    if LINEAR_MODE:
        # Keep wraparound in a 32-bit EA and fold the 64-bit arena base into
        # the actual memory operand. Integer/SSE operations do not need the
        # x87 data-pointer scratch store.
        return [ea]
    # The saved EA is at base RSP+520, plus eight while PUSHFQ is live.
    return ['    pushfq',ea,'    mov dword ptr [rsp+528], r10d',
        '    mov r11d, r10d','    shr r11d, 22','    mov r11, qword ptr [r9+r11*8]',
        '    test r11, r11',f'    jz {fault}',
        '    shr r10d, 12','    and r10d, 1023','    shl r10d, 4','    add r11, r10',
        '    mov r10d, dword ptr [r11+8]',f'    and r10d, {access}',f'    cmp r10d, {access}',f'    jne {fault}',
        *(['    test dword ptr [r11+8], 4',f'    jnz {fault}'] if access&2 else []),
        '    cmp qword ptr [r11], 0',f'    je {fault}',
        '    mov r10d, dword ptr [rsp+528]','    and r10d, 4095',f'    add r10d, {size}',
        '    cmp r10d, dword ptr [r11+12]',f'    ja {fault}',f'    sub r10d, {size}',
        '    add r10, qword ptr [r11]','    popfq']


def flag_policy(ins):
    mnemonic=ins.mnemonic
    if mnemonic=='idiv':return 0,0,None
    if mnemonic in ('add','adc','sub','sbb','cmp','neg'):return 0,STATUS,None
    if mnemonic in ('and','or','xor','test'):return AF,STATUS&~AF,None
    if mnemonic in ('inc','dec'):return 0,None,(0xffffffff,STATUS&~CF)
    if mnemonic=='sahf':return 0,None,(0xffffffff,STATUS&~OF)
    if mnemonic in ('comisd','ucomisd','comiss','ucomiss'):return 0,STATUS,None
    if mnemonic in ('imul','mul'):return STATUS&~(CF|OF),CF|OF,None
    if mnemonic in ('clc','stc','cmc'):return 0,None,(0xffffffff,CF)
    if mnemonic in SHIFTS:
        count=ins.operands[1].imm&31
        if not count:return 0,None,None
        width=ins.operands[0].size*8
        if mnemonic in ('rol','ror'):
            return 0 if count==1 else OF,None,(0xffffffff if count==1 else 0xffffffff^OF,CF|(OF if count==1 else 0))
        defined=(STATUS&~(AF|OF))|(OF if count==1 else 0)
        if mnemonic in ('shl','sal','shr') and count>=width:defined&=~CF
        return AF|(OF if count!=1 else 0),defined,None
    return 0,None,None


def save_status(shared=False):
    # LAHF + SETO preserve precisely the six arithmetic status bits without
    # POPFQ's serialization. No emitted guest instruction changes DF/IF/TF.
    return (['    mov dword ptr [r8+596], eax','    seto al','    lahf','    mov dword ptr [r8+600], eax'] if shared else
        ['    mov r15d, eax','    seto al','    lahf','    mov r10d, eax'])


def restore_status(shared=False):
    return ['    mov eax, '+('dword ptr [r8+600]' if shared else 'r10d'),'    add al, 127','    sahf',
        '    mov eax, '+('dword ptr [r8+596]' if shared else 'r15d')]


def materialize_flags(mask):
    if not mask:return []
    # Merge retained undefined values into LAHF/SETO's compact encoding.
    # MUL needs PF/ZF/SF and multi-bit shifts need OF; neither requires the
    # serializing POPFQ previously executed at each materialization boundary.
    assert not (mask&~STATUS)
    encoded=((mask&0xff)<<8)|bool(mask&OF)
    out=[*save_status(),f'    and r10d, {0xffffffff^encoded}']
    if mask&0xff:out += ['    mov r11d, dword ptr [r8+60]',f'    and r11d, {mask&0xff}',
        '    shl r11d, 8','    or r10d, r11d']
    if mask&OF:out += ['    mov r11d, dword ptr [r8+60]','    shr r11d, 11','    and r11d, 1','    or r10d, r11d']
    return out+restore_status()


def consumed_flags(ins):
    match=re.fullmatch('(set|cmov)('+CONDITION+')',ins.mnemonic)
    if match:
        return {'o':OF,'no':OF,'b':CF,'ae':CF,'e':0x40,'ne':0x40,'be':CF|0x40,'a':CF|0x40,
            's':0x80,'ns':0x80,'p':4,'np':4,'l':0x80|OF,'ge':0x80|OF,
            'le':0x40|0x80|OF,'g':0x40|0x80|OF}[match[2]]
    return CF if ins.mnemonic in ('adc','sbb','cmc') else 0


def emit_state_apis(assembly,fault_sites):
    out=['    mov r10, qword ptr [r8+624]','    test r10, r10','    jz .L_native_dynamic_out',
        '    cmp qword ptr [r8+632], 0','    je .L_native_dynamic_out',
        '    mov r11d, dword ptr [r8+32]','    sub r11d, 0xf0000000','    cmp r11d, 0xffff','    ja .L_native_dynamic_out',
        '    test r11d, 15','    jnz .L_native_dynamic_out','    shr r11d, 4','    lea r11d, [r11+r11*2]','    shl r11d, 4','    add r10, r11',
        '    cmp dword ptr [r10+20], 1','    jb .L_native_dynamic_out','    cmp dword ptr [r10+20], 7','    ja .L_native_dynamic_out',
        '    cmp r13d, r14d','    jae .L_native_dynamic_out','    cmp r12d, 0xfffffff4','    ja .L_native_dynamic_out']
    def load(offset,temporary):
        site=f'.L_native_memory_site_{len(fault_sites)}'
        fault_sites.append((site,site+'_end','.L_native_dynamic_out'))
        out.extend([site+':',f'    mov r11d, dword ptr [r9+r12+{offset}]',site+'_end:',f'    mov dword ptr [rsp+{temporary}], r11d'])
    for offset,temporary in ((0,528),(4,532),(8,536)):load(offset,temporary)
    out += ['    mov r11d, dword ptr [rsp+532]','    cmp r11d, dword ptr [r10+16]','    jne .L_native_dynamic_out',
        '    mov dword ptr [rsp+548], 12','    cmp dword ptr [r10+20], 4','    je .L_native_api_fvf_same',
        '    cmp r12d, 0xfffffff0','    ja .L_native_dynamic_out']
    load(12,540)
    out += ['    cmp dword ptr [r10+20], 5','    jae .L_native_api_resource_same',
        '    mov dword ptr [rsp+548], 16','    cmp dword ptr [r10+20], 1','    je .L_native_api_render',
        '    cmp r12d, 0xffffffec','    ja .L_native_dynamic_out']
    load(16,544)
    out += ['    mov dword ptr [rsp+548], 20','    mov r11d, dword ptr [rsp+536]',
        '    cmp r11d, dword ptr [r10+12]','    jae .L_native_api_invalid',
        '    mov eax, dword ptr [rsp+540]','    cmp eax, dword ptr [r10+8]','    jae .L_native_api_invalid',
        '    imul r11d, dword ptr [r10+8]','    add r11d, eax','    mov eax, dword ptr [rsp+544]',
        '    jmp .L_native_api_store','.L_native_api_render:',
        '    mov r11d, dword ptr [rsp+536]','    cmp r11d, dword ptr [r10+8]','    jae .L_native_api_invalid',
        '    mov eax, dword ptr [rsp+540]','    test eax, eax','    jz .L_native_api_store']
    for blocked in (28,52,137):out += [f'    cmp r11d, {blocked}','    je .L_native_dynamic_out']
    out += ['.L_native_api_store:','    mov r15, qword ptr [r10]','    cmp dword ptr [r15+r11*4], eax','    je .L_native_api_same',
        '    mov r15d, dword ptr [r15+r11*4]','    mov dword ptr [rsp+532], r15d','    mov r15, qword ptr [r10]',
        '    mov dword ptr [r15+r11*4], eax','    mov r15, qword ptr [r10+40]','    test r15, r15','    jz .L_native_api_changed',
        '    mov r15d, dword ptr [r15+r11*4]','    cmp r15d, dword ptr [rsp+532]','    je .L_native_api_changed',
        '    cmp r15d, eax','    jne .L_native_api_same','    mov r15, qword ptr [r10+32]','    test r15, r15','    jz .L_native_api_same',
        '    sub qword ptr [r15], 1','    jmp .L_native_api_same','.L_native_api_changed:',
        '    mov r15, qword ptr [r10+32]','    test r15, r15','    jz .L_native_api_same','    add qword ptr [r15], 1','.L_native_api_same:',
        '    mov dword ptr [r8+596], 0','    jmp .L_native_api_commit','.L_native_api_invalid:',
        '    mov dword ptr [r8+596], 0x8876086c','    jmp .L_native_api_commit',
        '.L_native_api_fvf_same:','    mov r15, qword ptr [r10]','    mov r11d, dword ptr [rsp+536]',
        '    cmp dword ptr [r15], r11d','    jne .L_native_dynamic_out','    jmp .L_native_api_same',
        '.L_native_api_resource_same:','    mov dword ptr [rsp+548], 16','    mov r15, qword ptr [r10]',
        '    mov r11d, dword ptr [rsp+540]','    cmp r11d, 0xffffffff','    je .L_native_dynamic_out',
        '    cmp dword ptr [r10+20], 6','    jae .L_native_api_texture_same',
        '    cmp dword ptr [rsp+536], 0','    jne .L_native_dynamic_out','    test r11d, r11d','    jz .L_native_dynamic_out',
        '    cmp dword ptr [r15+4], 0','    je .L_native_dynamic_out','    cmp dword ptr [r15], r11d','    jne .L_native_dynamic_out',
        '    jmp .L_native_api_same','.L_native_api_texture_same:',
        '    mov eax, dword ptr [rsp+536]','    cmp eax, dword ptr [r10+8]','    jae .L_native_dynamic_out',
        '    cmp dword ptr [r10+20], 7','    je .L_native_api_texture_binding',
        '    cmp dword ptr [r15+rax*4], r11d','    jne .L_native_dynamic_out','    jmp .L_native_api_same',
        '.L_native_api_texture_binding:',
        '    cmp qword ptr [r10+32], 0','    je .L_native_dynamic_out',
        '    test r11d, r11d','    jz .L_native_api_texture_valid',
        '    mov r15, qword ptr [r10+40]','    test r15, r15','    jz .L_native_dynamic_out',
        '    mov eax, r11d','    shr eax, 16','    cmp eax, dword ptr [r10+12]','    jae .L_native_dynamic_out',
        '    cmp dword ptr [r15+rax*4], r11d','    jne .L_native_dynamic_out',
        '.L_native_api_texture_valid:',
        '    mov eax, dword ptr [rsp+536]','    mov r15, qword ptr [r10]',
        '    cmp dword ptr [r15+rax*4], r11d','    je .L_native_api_same',
        '    mov dword ptr [r15+rax*4], r11d','    mov r15, qword ptr [r10+32]','    add qword ptr [r15], 1','    jmp .L_native_api_same',
        '.L_native_api_commit:',
        '    mov r11, qword ptr [r10+24]','    add qword ptr [r11], 1',
        '    mov r11, qword ptr [r8+632]','    add qword ptr [r11], 1',
        '    add dword ptr [r8+640], 1','    dec r14d','    add r12d, dword ptr [rsp+548]',
        '    mov r11d, dword ptr [rsp+528]','    mov dword ptr [r8+32], r11d','    jmp .L_native_dispatch_saved']
    assembly += out


def emit_instruction(ins,info,fault):
    mnemonic=ins.mnemonic
    out=[]
    operand=info['memory']
    if info.get('signed_divide'):
        divisor=render(ins,ins.operands[0]);ready=fault+'_divide_ready';failed=fault+'_divide_out';done=fault+'_divide_done'
        # A sign-extended 32-bit dividend can overflow only for INT_MIN / -1.
        # Other 64-bit dividends and arithmetic faults use the exact oracle.
        return [*save_status(),'    mov r11d, r15d','    sar r11d, 31','    cmp edx, r11d',f'    jne {failed}',
            f'    cmp {divisor}, 0',f'    je {failed}','    cmp r15d, 0x80000000',f'    jne {ready}',
            f'    cmp {divisor}, -1',f'    je {failed}',ready+':','    mov eax, r15d',f'    idiv {divisor}',
            '    mov r15d, eax',*restore_status(),f'    jmp {done}',failed+':',*restore_status(),
            *([] if LINEAR_MODE else ['    pushfq']),f'    jmp {fault}',done+':']
    if info.get('rep_stos'):
        if not LINEAR_MODE:return ['    pushfq',f'    jmp {fault}']
        return [*save_status(),'    test dword ptr [r8+616], 1024',f'    jnz {fault}_rep_out',
            '    mov eax, dword ptr [r8+612]','    add rax, rcx','    mov r11d, 0xffffffff','    cmp rax, r11',f'    ja {fault}_rep_out',
            '    mov r11, qword ptr [r8+648]','    sub r11, r13','    mov eax, dword ptr [r8+612]','    sub r11, rax',
            '    mov eax, dword ptr [r8+640]','    sub r11, rax','    dec r11','    mov eax, ecx','    cmp rax, r11',f'    ja {fault}_rep_out',
            '    sub r11, rax','    lea r11, [r11+r13+1]','    cmp r11, r14','    cmovb r14d, r11d','    add dword ptr [r8+644], 1',
            *restore_status(),'    mov r11d, ecx','    lea rdi, [r9+rdi]',
            *(['    lea rsi, [r9+rsi]'] if info.get('rep_move') else []),
            '    '+mnemonic,'    pushfq','    sub rdi, r9','    mov edi, edi',
            *(['    sub rsi, r9','    mov esi, esi'] if info.get('rep_move') else []),
            '    add dword ptr [r8+612], r11d','    popfq',
            f'    jmp {fault}_rep_done',f'{fault}_rep_out:',*restore_status(),f'    jmp {fault}',f'{fault}_rep_done:']
    if info.get('alignment'):
        out += ['    pushfq',address(ins,operand),f'    test r10d, {info["alignment"]-1}',
            f'    jz {fault}_aligned',*(['    popfq'] if LINEAR_MODE else []),f'    jmp {fault}',f'{fault}_aligned:','    popfq']
    if mnemonic in ('call','jmp') and ins.operands[0].type!=cs.x86.X86_OP_IMM:
        if operand:out+=memory_check(ins,operand,4,1,fault)+['    mov r11d, dword ptr [r10]']
        else:out += [f'    mov r11d, {render(ins,ins.operands[0])}']
        out += ['    mov dword ptr [r8+32], r11d']
        if mnemonic=='call':
            out+=memory_check(ins,None,4,2,fault)+[f'    mov dword ptr [r10], {ins.address+ins.size}','    lea r12d, [r12-4]']
        return out
    if operand:out+=memory_check(ins,operand,info.get('memory_size',operand.size),info['access'],fault)
    elif mnemonic in ('push','pop','call','ret','leave'):
        size=ins.operands[0].size if mnemonic in ('push','pop') else 4
        out+=memory_check(ins,None,size,info['access'],fault)
    if info.get('sse'):
        return out+['    '+mnemonic+' '+', '.join(render(ins,operand,True,info.get('memory_size')) for operand in ins.operands)]
    if info.get('mxcsr_store'):return out+['    stmxcsr dword ptr [r10]']
    if info.get('control_store'):
        return out+['    mov r11w, word ptr [r8+604]','    mov word ptr [r10], r11w']
    if info.get('control_load'):
        return out+['    fldcw word ptr [r10]','    fnstcw word ptr [r8+604]','    mov dword ptr [r8+608], 1']
    if mnemonic=='fwait':return out
    if mnemonic=='sahf':
        return out+['    sahf']
    if info['fp']:
        encoded=ins.bytes if not operand else (bytes([0x43,ins.bytes[0],(ins.bytes[1]&0x38)|4,0x11]) if LINEAR_MODE else bytes([0x41,ins.bytes[0],(ins.bytes[1]&0x38)|2]))
        out+=['    .byte '+','.join(f'0x{byte:02x}' for byte in encoded)]
        if info.get('fp_status'):return out
        if operand:out += ['    mov dword ptr [r8+48], r10d'] if LINEAR_MODE else ['    mov r11d, dword ptr [rsp+520]','    mov dword ptr [r8+48], r11d']
        return out
    if mnemonic=='lea':
        out+=[address(ins,ins.operands[1]),f'    mov {render(ins,ins.operands[0])}, {"r10w" if ins.operands[0].size==2 else "r10d"}']
        return out
    if mnemonic=='push':
        size=ins.operands[0].size
        out += [f'    mov {"word" if size==2 else "dword"} ptr [r10], {render(ins,ins.operands[0])}',f'    lea r12d, [r12-{size}]']
        return out
    if mnemonic=='pop':
        size=ins.operands[0].size
        out += [f'    mov {"r11w" if size==2 else "r11d"}, {"word" if size==2 else "dword"} ptr [r10]',
                f'    lea r12d, [r12+{size}]',f'    mov {render(ins,ins.operands[0])}, {"r11w" if size==2 else "r11d"}']
        return out
    if mnemonic=='call':
        return out+[f'    mov dword ptr [r10], {ins.address+ins.size}','    lea r12d, [r12-4]']
    if mnemonic=='ret':
        cleanup=ins.operands[0].imm if ins.operands else 0
        return out+['    mov r11d, dword ptr [r10]',f'    lea r12d, [r12+{4+cleanup}]','    mov dword ptr [r8+32], r11d']
    if mnemonic=='leave':
        return out+['    mov r11d, dword ptr [r10]','    lea r12d, [rbp+4]','    mov ebp, r11d']
    if mnemonic=='jmp' or re.fullmatch('j'+CONDITION,mnemonic):return out
    preserved,defined,modify=flag_policy(ins)
    # AF has no native conditional branch consumer. Keep its retained value
    # separately instead of serializing the CPU with POPFQ after every TEST,
    # XOR and logical operation. Fallback/exit materializes the exact bit.
    preserved &= ~AF
    if preserved:out+=['    pushfq','    pop r15','    mov dword ptr [r8+60], r15d']
    out+=['    '+mnemonic+(' '+', '.join(render(ins,operand,True) for operand in ins.operands) if ins.operands else '')]
    return out


def fold_linear_operand(ins,info,lines):
    # Guest GPRs are always zero-extended. Common base+small-positive-offset
    # operands therefore need no separate EA instruction. A 32-bit overflow
    # reaches the arena's reserved suffix guard and resumes at the exact guest
    # instruction, where the checked path applies wraparound. Negative offsets
    # and complex indexes keep their explicit 32-bit EA.
    if ins.mnemonic in ('call','jmp') or (info['fp'] and 'fop' in info):return lines
    operand=info['memory'];expression=None;ea=None
    if operand:
        m=operand.mem;displacement=m.disp
        if not m.base and not m.index and 0<=(displacement&0xffffffff)<=0x7fffffff:
            value=displacement&0xffffffff;expression='r9'+(f'+{value}' if value else '')
        elif m.base and not m.index and 0<=displacement<=65520:
            expression='r9+'+BASE64[ins.reg_name(m.base)]+(f'+{displacement}' if displacement else '')
        ea=address(ins,operand)
    elif ins.mnemonic in ('pop','ret','leave'):
        expression='r9+'+('rbp' if ins.mnemonic=='leave' else 'r12')
        ea=memory_check(ins,None,4,1,'')[0]
    if expression is None:return lines
    matches=[index for index,line in enumerate(lines) if line==ea]
    if not matches:raise ValueError('Missing folded EA at '+hex(ins.address))
    del lines[matches[-1]]
    return [line.replace('ptr [r9+r10]',f'ptr [{expression}]') for line in lines]


def generate(linear=False):
    global LINEAR_MODE
    LINEAR_MODE=linear
    raw=(ROOT/'WA2_chs.exe').read_bytes()
    sha=hashlib.sha256(raw).hexdigest()
    if sha!=json.loads((ROOT/'config/wa2.json').read_text())['sha256']:raise ValueError('Image/profile mismatch')
    image=pefile.PE(data=raw)
    decoder=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32);decoder.detail=True
    graph=json.loads((ROOT/'logs/cfg.json').read_text())
    instructions={item['address']:item for item in graph['instructions']}
    assembly=['// Generated native x64 basic blocks; no runtime instruction decoder.','.intel_syntax noprefix','.text']
    declarations=['extern "C" unsigned wa2_native_run(void*,const Memory::NativeIndex* const*,unsigned);']
    page_descriptors=[];entries=[];entry_lengths={};rows=[];page_counts=[];fault_sites=[];trace_roots=set();trace_descriptors=[];trace_blocks=0;trace_instructions=0;deferred_defined_regions=0
    replaced_instructions=[];retired_instructions=[]
    profile_points=[('wa2_native_run',4)];shared_fault_tails={};omitted_faults=0;kept_faults=0
    assembly+=['.globl wa2_native_run','.seh_proc wa2_native_run','wa2_native_run:']
    for reg in ('rbx','rbp','rsi','rdi','r12','r13','r14','r15'):assembly += [f'    push {reg}',f'    .seh_pushreg {reg}']
    assembly += ['    sub rsp, 552','    .seh_stackalloc 552','    .seh_endprologue',
        '    mov r14d, r8d','    mov r8, rcx','    mov r9, rdx','    xor r13d, r13d',
        '    pushfq','    pop r11','    mov qword ptr [rsp+512], r11']
    for index,reg in enumerate(('eax','ecx','edx','ebx','r12d','ebp','esi','edi')):assembly+=[f'    mov {reg}, dword ptr [r8+{index*4}]']
    assembly+=['    mov r11d, dword ptr [rsp+512]',f'    and r11d, {0xffffffff^STATUS}',
        '    or r11d, dword ptr [r8+36]','    push r11','    popfq','    jmp .L_native_dynamic']
    for page in PAGES:
        nodes={}
        for pc_hex,body in CASE.findall((ROOT/f'generated/wa2-win64/code_page_{page}.cpp').read_text()):
            pc=int(pc_hex,16)
            # Keep complete render call chains in hardware registers. The
            # independently tested C++ kernels remain available as fallback.
            if pc in SPECIALIZED_ENTRIES and pc not in NATIVE_ENTRIES|{0x47e798,0x43b290,0x43bde0,0x43bba8,0x43cc4d,0x43cd37,0x464aa1}:continue
            encoded=bytes.fromhex(instructions[pc]['bytes'])
            if image.get_data(pc-image.OPTIONAL_HEADER.ImageBase,len(encoded))!=encoded:raise ValueError('Image/graph mismatch')
            guard=re.match(r'wr::check_code\(m,s.eip,\{([\d,]+)\}\);\n',body)
            if not guard or bytes(map(int,guard[1].split(',')))!=encoded:raise ValueError('Oracle guard mismatch')
            decoded=list(decoder.disasm(encoded,pc))
            if len(decoded)!=1 or decoded[0].size!=len(encoded):raise ValueError('Instruction boundary mismatch')
            ins=decoded[0]
            if bytes(ins.bytes)==b'\xf3\xc3':ins=ReturnHint(ins)
            semantics=re.sub(r'\ns.eip=0x[0-9a-f]+u;$','',body[guard.end():])
            info=select(ins,semantics)
            if info:
                if any(start<=pc<end for start,end in NATIVE_RANGES):
                    replaced_instructions.append(pc)
                elif retired_contains(pc):retired_instructions.append(pc)
                else:nodes[pc]=(ins,info)
        terminal=lambda ins:ins.mnemonic in ('jmp','call','ret','fldcw','idiv') or ins.mnemonic.startswith(('rep stos','rep movs')) or bool(re.fullmatch('j'+CONDITION,ins.mnemonic))
        starts={block['start'] for block in graph['blocks'] if block['start'] in nodes}
        starts.update(pc for pc in graph['function_candidates'] if pc in nodes)
        starts.update(pc for pc,(_,info) in nodes.items() if info.get('rep_stos'))
        starts.update(pc for pc,(_,info) in nodes.items() if info.get('signed_divide'))
        predecessors=Counter()
        for pc,(ins,info) in nodes.items():
            next_pc=pc+ins.size
            if terminal(ins):
                if next_pc in nodes:starts.add(next_pc)
                if ins.mnemonic not in ('ret','fldcw') and not info.get('rep_stos') and ins.operands[0].type==cs.x86.X86_OP_IMM and ins.operands[0].imm in nodes:starts.add(ins.operands[0].imm)
            elif next_pc in nodes:predecessors[next_pc]+=1
        starts.update(pc for pc in nodes if predecessors[pc]!=1)
        blocks={};visited=set()
        for start in sorted(nodes):
            if start in visited:continue
            group=[];current=start
            while current in nodes and current not in visited:
                if group and current in starts:break
                group.append(current);visited.add(current)
                ins=nodes[current][0];current+=ins.size
                if terminal(ins) or len(group)==MAX_BLOCK_INSTRUCTIONS:break
            blocks[start]=group
        entries.extend(blocks)
        entry_lengths.update((start,len(group)) for start,group in blocks.items())
        rows.extend((pc,nodes[pc][0].size) for pc in nodes)
        if page in HOT_PAGES:
            verifier=f'[](Memory& m){{return wa2_block_code_verified(m,0x{page<<12:x}u);}}'
        else:
            code=image.get_data((page<<12)-image.OPTIONAL_HEADER.ImageBase,4096)
            code=code.ljust(4096,b'\0')
            declarations += [f'static constexpr std::array<std::uint8_t,4096> native_signature_{page}={{'+','.join(map(str,code))+'};']
            verifier=f'[](Memory& m){{return m.matches_static_code_page<native_signature_{page}>(0x{page<<12:x}u);}}'
        verifier=verifier.replace('return ','return WA2_NATIVE_VERIFY_PAGE(m,0x'+f'{page<<12:x}'+'u,').replace(';}',');}')
        page_descriptors.append(f'{{0x{page<<12:x}u,{verifier}}},')
        out=[];parts={}
        exits=set();faults={}
        def destination(target):
            if target in blocks:return f'.L_native_{page}_entry_{target:x}'
            exits.add(target);return f'.L_native_{page}_out_{target:x}'
        def fp_metadata(group):
            for pc in reversed(group):
                info=nodes[pc][1]
                if info['fp'] and 'fop' in info and not info.get('fp_status'):
                    return [f'    mov dword ptr [r8+44], {pc}',f'    mov dword ptr [r8+52], {info["fop"]}']
            return []
        def sse_metadata(group):
            normal=pure=False
            for pc in group:
                info=nodes[pc][1]
                if info.get('sse'):
                    if info.get('normalize_control',True):normal=True
                    else:pure=True
            # Admission requires a canonical CW, and FLDCW explicitly updates
            # its shadow. SSE never changes that word; it needs no FNSTCW.
            return (['    mov dword ptr [r8+592], 1'] if normal else [])+(['    mov dword ptr [r8+620], 1'] if pure else [])
        def capture_af():return ['    pushfq','    pop r11','    mov dword ptr [r8+588], r11d']
        def flag_transform(group):
            # Defined/undefined provenance is inspected only at C++ exits.
            # Compose its exact transform once per block, including each
            # fault prefix, instead of updating it after every arithmetic op.
            keep,bits=STATUS,0
            for pc in group:
                _,defined,modify=flag_policy(nodes[pc][0])
                if defined is not None:keep,bits=0,defined
                elif modify:keep&=modify[0];bits=(bits&modify[0])|modify[1]
            return keep,bits
        def flag_metadata(group):return defined_metadata(*flag_transform(group))
        for start,group in blocks.items():
            first_site=len(fault_sites)
            profile_points.append((f'.L_native_{page}_entry_{start:x}',start|0x80000000))
            # Retain the oracle's undefined flag values without POPFQ after
            # every multiply/shift. Restore only before a real consumer, an
            # exit, or a precise fault prefix; a later arithmetic overwrite
            # can make the saved bits dead without materializing them.
            pending=0;pending_states=[0];before_flags={}
            for pc in group:
                ins,info=nodes[pc];preserved,defined,modify=flag_policy(ins);preserved&=~AF
                written=defined if defined is not None else modify[1] if modify else 0
                if pending&consumed_flags(ins) or (pending and preserved and (info['memory'] or pending&~written)):
                    before_flags[pc]=materialize_flags(pending);pending=0
                pending=(pending&~written)|preserved;pending_states.append(pending)
            before_af={};af_states=[False];valid_af=False
            for pc in group:
                ins,info=nodes[pc]
                if flag_policy(ins)[0]&AF:
                    if valid_af:before_af[pc]=capture_af()
                    valid_af=False
                if ins.mnemonic in ('add','adc','sub','sbb','cmp','neg','inc','dec','sahf') or info.get('sse_flags'):valid_af=True
                af_states.append(valid_af)
            alignment_checks=set();written=set()
            if linear:
                for pc in group:
                    ins,info=nodes[pc];alignment=info.get('alignment',0);operand=info.get('memory')
                    if alignment and operand:
                        m=operand.mem
                        if not m.base and not m.index and not ((m.disp&0xffffffff)%(alignment)):
                            info['alignment']=0
                        elif m.base and not m.index:
                            base=ins.reg_name(m.base)
                            if base not in written:
                                alignment_checks.add((base,m.disp&(alignment-1),alignment));info['alignment']=0
                    registers=list(ins.regs_access()[1])
                    if ins.mnemonic=='xchg':registers.extend(operand.reg for operand in ins.operands if operand.type==cs.x86.X86_OP_REG)
                    elif (ins.mnemonic in {'mov','movsx','movzx','lea','add','sub','adc','sbb','and','or','xor','inc','dec','neg','not','imul','bswap'}|SHIFTS or re.fullmatch('(set|cmov)'+CONDITION,ins.mnemonic)) and ins.operands and ins.operands[0].type==cs.x86.X86_OP_REG:
                        registers.append(ins.operands[0].reg)
                    if ins.mnemonic in ('mul','idiv') or (ins.mnemonic=='imul' and len(ins.operands)==1):registers.extend((cs.x86.X86_REG_EAX,cs.x86.X86_REG_EDX))
                    if ins.mnemonic in ('cdq','cwd'):registers.append(cs.x86.X86_REG_EDX)
                    if ins.mnemonic=='cwde':registers.append(cs.x86.X86_REG_EAX)
                    for register in registers:
                        family=REG_FAMILY.get(ins.reg_name(register))
                        if family:written.add(family)
            out += [f'.L_native_{page}_entry_{start:x}:']
            out += [*save_status(),'    mov r11d, r14d','    sub r11d, r13d',
                    f'    cmp r11d, {len(group)}',f'    jb .L_native_{page}_budget_{start:x}']
            if any(pc+nodes[pc][0].size>(page+1)*4096 for pc in group):
                # Crossing instructions require both immutable page proofs.
                out += ['    mov r11, qword ptr [r8+576]',f'    cmp byte ptr [r11+{page+1-0x400}], 0',f'    je .L_native_{page}_budget_{start:x}']
            for base,offset,alignment in sorted(alignment_checks):
                register='r15d' if base=='eax' else REGS[base]
                if offset:
                    host='r15' if base=='eax' else BASE64[base]
                    out += [f'    lea r11d, [{host}+{offset}]'];register='r11d'
                out += [f'    test {register}, {alignment-1}',f'    jnz .L_native_{page}_budget_{start:x}']
            if any(nodes[pc][1]['fp'] for pc in group):
                out += ['    cmp dword ptr [r8+56], 0',f'    jne .L_native_{page}_fp_ready_{start:x}',
                    '    fxsave64 [rsp]','    fxrstor64 [r8+64]','    mov dword ptr [r8+56], 1',
                    f'.L_native_{page}_fp_ready_{start:x}:']
            out += restore_status()
            body_offset=len(out)
            out += [f'.L_native_{page}_body_{start:x}:']
            profile_points.append((f'.L_native_{page}_body_{start:x}',start))
            for index,pc in enumerate(group):
                ins,info=nodes[pc]
                fault=f'.L_native_{page}_fault_{pc:x}'
                prefix=group[:index]
                faults[pc]=(index,materialize_flags(pending_states[index])+fp_metadata(prefix)+sse_metadata(prefix)+flag_metadata(prefix)+(capture_af() if af_states[index] else []))
                emitted=before_flags.get(pc,[])+before_af.get(pc,[])+emit_instruction(ins,info,fault)
                if linear:emitted=fold_linear_operand(ins,info,[line.replace('ptr [r10]','ptr [r9+r10]') for line in emitted])
                out += [f'    // {pc:#x}: {ins.mnemonic} {ins.op_str}']
                for line in emitted:
                    guest_memory='ptr [r10]' in line or bool(re.search(r'ptr \[r9(?:\+|\])',line)) or (info['fp'] and info['memory'] and line.startswith('    .byte')) or line.startswith(('    rep stos','    rep movs'))
                    if linear and guest_memory:
                        site=f'.L_native_memory_site_{len(fault_sites)}'
                        fault_sites.append((site,site+'_end',fault+'_rep' if info.get('rep_stos') else fault))
                        out += [site+':',line,site+'_end:']
                    else:out.append(line)
            last=nodes[group[-1]][0];next_pc=last.address+last.size
            out += [f'.L_native_{page}_metadata_{start:x}:']
            profile_points.append((f'.L_native_{page}_metadata_{start:x}',start|0x20000000))
            out += materialize_flags(pending)+fp_metadata(group)+sse_metadata(group)
            defined_begin=len(out)-body_offset;out+=flag_metadata(group);defined_end=len(out)-body_offset
            out += (capture_af() if af_states[-1] else [])+[f'    lea r13d, [r13+{len(group)}]']
            if last.mnemonic=='fldcw':
                # A guest may unmask exceptions with FLDCW. Commit the control
                # instruction, then leave before executing any following x87
                # operation with an unmasked host exception.
                out += [*save_status(),'    mov r11d, dword ptr [r8+604]','    and r11d, 63','    cmp r11d, 63',
                    f'    jne .L_native_{page}_control_out_{start:x}',*restore_status(),f'    jmp {destination(next_pc)}',
                    f'.L_native_{page}_control_out_{start:x}:',*restore_status(),f'    mov dword ptr [r8+32], {next_pc}','    jmp .L_native_exit']
            elif last.mnemonic=='ret' or (last.mnemonic in ('jmp','call') and last.operands[0].type!=cs.x86.X86_OP_IMM):
                out += ['    jmp .L_native_dynamic']
            elif last.mnemonic in ('jmp','call'):out += [f'    jmp {destination(last.operands[0].imm)}']
            elif re.fullmatch('j'+CONDITION,last.mnemonic):
                out += [f'    {last.mnemonic} {destination(last.operands[0].imm)}',f'    jmp {destination(next_pc)}']
            else:out += [f'    jmp {destination(next_pc)}']
            parts[start]=dict(body=out[body_offset:].copy(),sites=fault_sites[first_site:].copy(),
                defined_slice=(defined_begin,defined_end),defined_transform=flag_transform(group),
                repeat=any(nodes[pc][1].get('rep_stos') for pc in group),
                eligible=not alignment_checks and not any(nodes[pc][1].get('rep_stos') or nodes[pc][0].mnemonic=='fldcw' or pc+nodes[pc][0].size>(page+1)*4096 for pc in group))
            out += [f'.L_native_{page}_budget_{start:x}:',*restore_status(),f'    mov dword ptr [r8+32], {start}','    jmp .L_native_exit']
            profile_points.append((f'.L_native_{page}_budget_{start:x}',start|0x40000000))
        references={match[0] for line in out if not line.endswith(':') for match in re.finditer(r'\.L_native_[A-Za-z0-9_]+',line)}
        references.update(site[2] for site in fault_sites)
        for pc,(prefix,metadata) in faults.items():
            fault=f'.L_native_{page}_fault_{pc:x}'
            if linear and fault not in references and fault+'_rep' not in references:
                omitted_faults+=1;continue
            kept_faults+=1
            if linear and nodes[pc][1].get('rep_stos'):
                profile_points.append((f'.L_native_{page}_fault_{pc:x}_rep',pc|0x10000000))
                out += [f'.L_native_{page}_fault_{pc:x}_rep:','    pushfq','    sub r11d, ecx',
                    '    add dword ptr [r8+612], r11d','    sub rdi, r9','    mov edi, edi',
                    *(['    sub rsi, r9','    mov esi, esi'] if nodes[pc][1].get('rep_move') else []),
                    '    popfq',f'    jmp .L_native_{page}_fault_{pc:x}']
            if linear and metadata and not any('r13' in line or '[r8+32]' in line for line in metadata):
                key=tuple(metadata);tail=shared_fault_tails.setdefault(key,f'.L_native_fault_metadata_{len(shared_fault_tails)}')
                out += [fault+':',f'    lea r13d, [r13+{prefix}]',f'    mov dword ptr [r8+32], {pc}',f'    jmp {tail}']
            else:
                out += [fault+':',*(['    popfq'] if not linear else []),*metadata,f'    lea r13d, [r13+{prefix}]',f'    mov dword ptr [r8+32], {pc}','    jmp .L_native_exit']
            profile_points.append((f'.L_native_{page}_fault_{pc:x}',pc|0x10000000))
        for target in sorted(exits):
            out += [f'.L_native_{page}_out_{target:x}:',f'    mov dword ptr [r8+32], {target}','    jmp .L_native_dynamic']
            profile_points.append((f'.L_native_{page}_out_{target:x}',(target&0x0fffffff)|0x60000000))
        regions=forward_regions(nodes,blocks,parts)
        trace_roots.update(regions)
        entry_labels={f'.L_native_{page}_entry_{root:x}':f'.L_native_{page}_trace_{root:x}' for root in regions}
        label_pattern=re.compile(r'\.L_native_[A-Za-z0-9_]+')
        def renamed(line,labels):return label_pattern.sub(lambda match:labels.get(match[0],match[0]),line)
        # Ordinary entries remain available for a short remaining budget.
        # Only incoming edges and the global table prefer the reserved path.
        out=[renamed(line,entry_labels) if not line.endswith(':') else line for line in out]
        for root,(members,maximum) in regions.items():
            trace_descriptors.append((root,maximum))
            label=f'.L_native_{page}_trace_{root:x}'
            profile_points.append((label,root|0x70000000))
            out += [label+':',*save_status(),'    mov r11d, r14d','    sub r11d, r13d',
                f'    cmp r11d, {maximum}',f'    jb {label}_short']
            if any(nodes[pc][1]['fp'] for start in members for pc in blocks[start]):
                out += ['    cmp dword ptr [r8+56], 0',f'    jne {label}_fp_ready',
                    '    fxsave64 [rsp]','    fxrstor64 [r8+64]','    mov dword ptr [r8+56], 1',
                    f'{label}_fp_ready:']
            out += restore_status()
            defined_states=region_defined_states(nodes,blocks,parts,root,members)
            if defined_states:deferred_defined_regions+=1
            fault_wrappers=[];exit_wrappers=[];ordered=sorted(members)
            for position,start in enumerate(ordered):
                body=parts[start]['body']
                if defined_states:
                    first,last=parts[start]['defined_slice'];body=body[:first]+body[last:]
                local={line[:-1]:line[:-1]+f'_trace_{root:x}' for line in body if line.endswith(':')}
                local[f'.L_native_{page}_body_{start:x}']=f'{label}_body_{start:x}'
                edges={f'.L_native_{page}_entry_{target:x}':f'{label}_body_{target:x}' for target in members if target>start}
                faults_map={};exit_map={}
                fault_names={f'.L_native_{page}_fault_{pc:x}' for pc in blocks[start]}
                if defined_states:
                    references={match[0] for line in body for match in label_pattern.finditer(line)}
                    for pc in blocks[start]:
                        recovery=f'.L_native_{page}_fault_{pc:x}'
                        if recovery in references or any(site[2]==recovery for site in parts[start]['sites']):
                            wrapper=f'{label}_defined_fault_{pc:x}';faults_map[recovery]=wrapper
                            fault_wrappers.append((wrapper,pc,defined_states[0][start],recovery))
                    for line in body:
                        branch=re.fullmatch(r'    j\w+ (\.L_native_[A-Za-z0-9_]+)',line)
                        if not branch:continue
                        target=branch[1]
                        if target in local or target in fault_names or target in edges:continue
                        if target not in exit_map:
                            wrapper=f'{label}_defined_edge_{start:x}_{len(exit_wrappers)}';exit_map[target]=wrapper
                            exit_wrappers.append((wrapper,start,defined_states[1][start],entry_labels.get(target,target)))
                labels={**entry_labels,**edges,**exit_map,**faults_map,**local}
                profile_points.append((local[f'.L_native_{page}_body_{start:x}'],start|0x30000000))
                profile_points.append((local[f'.L_native_{page}_metadata_{start:x}'],start|0x50000000))
                emitted=[renamed(line,labels) for line in body]
                if position+1<len(ordered) and emitted[-1]==f'    jmp {label}_body_{ordered[position+1]:x}':emitted.pop()
                out += emitted
                for first,last,recovery in parts[start]['sites']:
                    fault_sites.append((local[first],local[last],faults_map.get(recovery,local.get(recovery,recovery))))
                trace_blocks+=1;trace_instructions+=len(blocks[start])
            for wrapper,pc,state,target in exit_wrappers+fault_wrappers:
                is_fault='_fault_' in wrapper
                profile_points.append((wrapper,pc|(0x10000000 if is_fault else 0x60000000)))
                out += [wrapper+':']
                out+=defined_metadata(*state)
                out += [f'    jmp {target}']
            profile_points.append((label+'_short',root|0x40000000))
            out += [f'{label}_short:',*restore_status(),f'    jmp .L_native_{page}_entry_{root:x}']
        assembly+=out
        page_counts.append(dict(page=page,instructions=len(nodes),blocks=len(blocks),floating_instructions=sum(info['fp'] for _,info in nodes.values())))
    for metadata,label in shared_fault_tails.items():assembly += [label+':',*metadata,'    jmp .L_native_exit']
    assembly += ['.L_native_dynamic:',*save_status(shared=True),'.L_native_dispatch_saved:','    mov r11d, dword ptr [r8+32]',
        '    sub r11d, 0x400000','    cmp r11d, 0xaffff','    ja .L_native_external',
        '    mov r10d, r11d','    shr r10d, 12','    cmp r10d, dword ptr [r8+584]','    je .L_native_dynamic_out',
        '    mov r15, qword ptr [r8+576]','    cmp byte ptr [r15+r10], 0','    je .L_native_dynamic_out',
        '    lea r10, [rip+.L_native_entry_indices]','    movzx r11d, word ptr [r10+r11*2]',
        '    lea r10, [rip+.L_native_entries]','    movsxd r11, dword ptr [r10+r11*4]',
        '    lea r10, [r10+r11]',*restore_status(shared=True),'    jmp r10','.L_native_external:']
    profile_points += [('.L_native_dynamic',1),('.L_native_external',2)]
    if linear:emit_state_apis(assembly,fault_sites)
    else:assembly += ['    jmp .L_native_dynamic_out']
    assembly += ['.L_native_dynamic_out:',*restore_status(shared=True),'    jmp .L_native_exit',
        '.L_native_exit:','    pushfq','    pop r11','    mov dword ptr [r8+36], r11d',
        f'    and dword ptr [r8+36], {0xffffffff^AF}','    mov r15d, dword ptr [r8+588]',f'    and r15d, {AF}','    or dword ptr [r8+36], r15d',
        '    cmp dword ptr [r8+56], 0','    je .L_native_fp_saved',
        '    fxsave64 [r8+64]','    fxrstor64 [rsp]','.L_native_fp_saved:']
    for index,reg in enumerate(('eax','ecx','edx','ebx','r12d','ebp','esi','edi')):assembly+=[f'    mov dword ptr [r8+{index*4}], {reg}']
    assembly += ['    mov eax, r13d','    mov r11, qword ptr [rsp+512]','    push r11','    popfq','    lea rsp, [rsp+552]']
    for reg in ('r15','r14','r13','r12','rdi','rsi','rbp','rbx'):assembly += [f'    pop {reg}']
    assembly += ['    ret',*(['.globl wa2_native_linear_code_end','wa2_native_linear_code_end:'] if linear else []),'.seh_endproc','.section .rdata,"dr"','.p2align 3','.L_native_entries:']
    ordered_entries=sorted(set(entries))
    if len(ordered_entries)>=65536:raise ValueError('Native entry index no longer fits 16 bits')
    entry_indices={pc:n+1 for n,pc in enumerate(ordered_entries)}
    assembly += ['    .long .L_native_exit - .L_native_entries']
    for pc in ordered_entries:
        assembly += [f'    .long .L_native_{pc>>12}_{"trace" if pc in trace_roots else "entry"}_{pc:x} - .L_native_entries']
    assembly += ['.p2align 1',*(['.globl wa2_native_entry_indices','wa2_native_entry_indices:'] if linear else []),'.L_native_entry_indices:']
    for first in range(0x400000,0x4b0000,64):
        assembly += ['    .short '+','.join(str(entry_indices.get(pc,0)) for pc in range(first,first+64))]
    declarations += ['static constexpr NativePageCode native_code_pages[]={',*page_descriptors,'};',
        'static constexpr U32 native_entries[]={',*(f'0x{pc:x}u,' for pc in sorted(entries)),'};']
    test_metadata = ['static constexpr Wa2NativeForwardRegion native_forward_regions[]={',
        *(f'{{0x{pc:x}u,{maximum}}},' for pc,maximum in sorted(trace_descriptors)),'};',
        'static constexpr Wa2NativeInstruction native_instructions[]={',
        *(f'{{0x{pc:x}u,{length}}},' for pc,length in sorted(rows)),'};']
    if linear:
        assembly += ['.globl wa2_native_entry_lengths','wa2_native_entry_lengths:']
        lengths=[0]+[entry_lengths[pc] for pc in ordered_entries]
        for first in range(0,len(lengths),256):
            assembly += ['    .byte '+','.join(map(str,lengths[first:first+256]))]
        instructions_by_label={line[:-1]:assembly[n+1].strip() for n,line in enumerate(assembly[:-1]) if line.endswith(':')}
        exact_fault_sites=[];decoder=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_64)
        for first,last,recovery in fault_sites:
            line=instructions_by_label[first]
            if line.startswith('.byte '):
                raw=bytes(int(v.strip(),0) for v in line[6:].split(','));decoded=list(decoder.disasm(raw,0))
                if sum(i.size for i in decoded)!=len(raw):raise ValueError('Invalid native fault instruction bytes')
                exact_fault_sites.extend((first+(f' + {i.address}' if i.address else ''),recovery) for i in decoded)
            else:
                if '\n' in line or ';' in line or line.startswith('.'):raise ValueError('Fault span is not a single native instruction')
                exact_fault_sites.append((first,recovery))
        assembly += ['.p2align 2','.globl wa2_native_linear_fault_entry_bytes','wa2_native_linear_fault_entry_bytes:','    .long 6',
            '.globl wa2_native_linear_faults','wa2_native_linear_faults:']
        for first,recovery in exact_fault_sites:
            values=[]
            for label in (first,recovery):
                values.extend(f'(({label} - wa2_native_run) >> {shift}) & 255' for shift in (0,8,16))
            assembly += ['    .byte '+', '.join(values)]
        assembly += ['.globl wa2_native_linear_faults_end','wa2_native_linear_faults_end:']
        # Immutable host-PC to guest-block boundaries for an owned-thread CPU
        # sampler. Labels/table add no instructions to the execution path.
        assembly += ['.p2align 2','.globl wa2_native_linear_profile','wa2_native_linear_profile:','#if defined(WA2_NATIVE_PROFILE_POINTS)']
        for label,tag in profile_points+[('.L_native_dynamic_out',5),('.L_native_exit',3)]:
            assembly += [f'    .long {label} - wa2_native_run, {tag}']
        assembly += ['#endif','.globl wa2_native_linear_profile_end','wa2_native_linear_profile_end:']
        (ROOT/'port/native_linear.S').write_text('\n'.join(line.replace('.L_native','.L_linear').replace('wa2_native_run','wa2_native_linear_run') for line in assembly)+'\n')
    else:
        (ROOT/'port/native_backend.S').write_text('\n'.join(assembly)+'\n')
        (ROOT/'port/native_backend_data.inc').write_text('// Generated by scripts/generate_native_backend.py.\n'+'\n'.join(declarations)+'\n')
        (ROOT/'port/native_backend_test_data.inc').write_text('// Build-time test inventory; not linked into the game.\n'+'\n'.join(test_metadata)+'\n')
    manifest=dict(schema='wa2.native-register-backend.v1',input_sha256=sha,pages=page_counts,
        generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        assembly_sha256=hashlib.sha256((ROOT/('port/native_linear.S' if linear else 'port/native_backend.S')).read_bytes()).hexdigest(),
        instructions=len(rows),entries=len(entries),entry_points=sorted(entries),maximum_basic_block_instructions=MAX_BLOCK_INSTRUCTIONS,hardware_integer_flags=True,registers_resident=True,
        native_source_functions=NATIVE_FUNCTIONS['functions'],native_function_manifest_sha256=hashlib.sha256((ROOT/'port/native_functions.json').read_bytes()).hexdigest(),
        source_replaced_instruction_locations=replaced_instructions,source_replaced_instructions=len(replaced_instructions),checked_cpp_fallback_retained=True,
        retired_module_manifest_sha256=hashlib.sha256(RETIRED_MANIFEST.read_bytes()).hexdigest(),
        retired_module_instruction_locations=retired_instructions,retired_module_instructions=len(retired_instructions),
        retired_modules_have_test_oracle_only=True,
        forward_regions=len(trace_roots),forward_region_blocks=trace_blocks,forward_region_instructions=trace_instructions,maximum_forward_path_instructions=MAX_TRACE_INSTRUCTIONS,
        deferred_defined_flag_regions=deferred_defined_regions,
        fpu_resident_within_native_call=True,cpp_helpers_inside_blocks=False,
        memory_access='Locked native page descriptors; cross-page and executable writes yield to the oracle',
        boundary_state_only=True,cross_page_native_control_flow=True,
        native_linear_memory=linear,static_memory_fault_sites=len(exact_fault_sites) if linear else len(fault_sites),
        static_memory_fault_spans=len(fault_sites),static_memory_fault_table_entry_bytes=6 if linear else 0,
        omitted_unreferenced_fault_stubs=omitted_faults,retained_fault_stubs=kept_faults,shared_fault_metadata_tails=len(shared_fault_tails))
    (ROOT/('port/native_linear_manifest.json' if linear else 'port/native_backend_manifest.json')).write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({key:value for key,value in manifest.items() if key not in ('entry_points','pages')},indent=2))


if __name__=='__main__':generate();generate(linear=True)
