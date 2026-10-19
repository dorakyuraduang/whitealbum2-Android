"""Recover additional static entries from image pointers and callback constants.

These are heuristic entry candidates, not proof of whole-program coverage.
The original image is never modified or executed.
"""
from pathlib import Path
import bisect
import hashlib
import json
import re
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'/'python'))
import capstone
import pefile

def main():
    exe=ROOT/'WA2_chs.exe';tool=ROOT/'build'/'winrecomp'/'winrecomp.exe'
    pe=pefile.PE(str(exe));base=pe.OPTIONAL_HEADER.ImageBase
    sections=[s for s in pe.sections if s.Characteristics&0x20000000]
    def is_code(va):
        return any(base+s.VirtualAddress<=va<base+s.VirtualAddress+min(s.Misc_VirtualSize,s.SizeOfRawData) for s in sections)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    graph=json.loads((ROOT/'logs'/'cfg-initial.json').read_text())
    seeds={};rejected={};history=[]
    for round_number in range(1,7):
        known={i['address'] for i in graph['instructions']};ordered=sorted(known)
        sizes={i['address']:i['size'] for i in graph['instructions']}
        def offer(va,source):
            if va in known or va in seeds or va in rejected or not is_code(va):return
            at=bisect.bisect_right(ordered,va)-1
            if at>=0 and ordered[at]+sizes[ordered[at]]>va:return
            raw=pe.get_data(va-base,16)
            first=next(decoder.disasm(raw,va,count=1),None)
            if not first or first.mnemonic in ('int3','hlt','ud2'):return
            seeds[va]=source
        for section in pe.sections:
            if section.Name.rstrip(b'\0') not in (b'.rdata',b'.data'):continue
            raw=section.get_data()
            string_bytes=bytearray(len(raw))
            for pattern in (rb'[\x20-\x7e]{3,}\x00',rb'(?:[\x00-\x7f]\x00){4,}'):
                for match in re.finditer(pattern,raw):string_bytes[match.start():match.end()]=bytes([1])*(match.end()-match.start())
            for library in pe.DIRECTORY_ENTRY_IMPORT:
                for item in library.imports:
                    if not item.name:continue
                    at=pe.get_offset_from_rva(item.hint_name_table_rva)-section.PointerToRawData
                    if 0<=at<len(raw):
                        end=min(len(raw),at+2+len(item.name)+1)
                        string_bytes[at:end]=bytes([1])*(end-at)
            for offset in range(0,len(raw)-3,4):
                if any(string_bytes[offset:offset+4]):continue
                value=struct.unpack_from('<I',raw,offset)[0]
                offer(value,f"{section.Name.rstrip(bytes([0])).decode()}:0x{base+section.VirtualAddress+offset:08x}")
        for instruction in graph['instructions']:
            if instruction['mnemonic'] not in ('mov','push','lea'):continue
            for op in instruction['operands']:
                if op['kind']=='immediate' and not op.get('relative'):
                    offer(int(op['value_hex'],16),f"callback/constant:0x{instruction['address']:08x}")
        # Indexed absolute jump tables provide further candidate entries. Every
        # runtime target is still checked against a compiled instruction body.
        for instruction in graph['instructions']:
            if instruction['mnemonic']!='jmp' or not instruction['operands']:continue
            op=instruction['operands'][0]
            if op.get('kind')!='memory' or op.get('base')!='none' or op.get('index')=='none' or op.get('scale')!=4:continue
            table=op['displacement']&0xffffffff
            for index in range(4096):
                try:value=pe.get_dword_at_rva(table-base+index*4)
                except pefile.PEFormatError:break
                if value is None or not is_code(value):break
                offer(value,f"jump-table:0x{table:08x}+{index*4}")
        previous=set(graph['roots'])
        fresh=sorted(set(seeds)-previous)
        if not fresh:break
        if len(seeds)>1500:raise RuntimeError('Seed list exceeds the bounded command-line profile')
        output=ROOT/'logs'/'cfg.json'
        command=[str(tool),'cfg',str(exe),str(output)]
        for seed in sorted(seeds):command+=['--seed',hex(seed)]
        subprocess.run(command,check=True)
        candidate=json.loads(output.read_text())
        if candidate['diagnostics']:
            # Never silently admit overlapping/invalid code. Keep the candidates
            # and evidence for inspection instead of generating a partial graph.
            (ROOT/'logs'/'seed-candidates.json').write_text(json.dumps({str(k):v for k,v in seeds.items()},indent=2)+'\n')
            raise RuntimeError('Seeded graph has discovery diagnostics: '+repr(candidate['diagnostics'][:20]))
        graph=candidate
        history.append({'round':round_number,'seeds':len(seeds),'instructions':len(graph['instructions']),'blocks':len(graph['blocks'])})
        print(json.dumps(history[-1]),flush=True)
    else:
        raise RuntimeError('Seed discovery did not converge within six rounds')
    if not (ROOT/'logs'/'cfg.json').exists():(ROOT/'logs'/'cfg.json').write_text(json.dumps(graph))
    profile={'schema':'winrecomp.runtime-profile.v1','sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'seeds':sorted(seeds),'seed_sources':{f'0x{k:08x}':v for k,v in sorted(seeds.items())},'discovery_history':history,'whole_program_complete':False}
    (ROOT/'config').mkdir(exist_ok=True)
    (ROOT/'config'/'wa2.json').write_text(json.dumps(profile,indent=2)+'\n')
    print(f"Final: {len(graph['instructions'])} instructions, {len(seeds)} extra roots")

if __name__=='__main__':main()
