"""Statically cache the explicitly authorized Special Contents input.

Never opens WA2_chs.exe, executes a source program, or touches original PAKs.
Generated C++/CFG/image data stay ignored and are shared by every host.
"""
from __future__ import annotations
import argparse
import bisect
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/python'))
import capstone
import pefile


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def candidates(pe, graph, seeds):
    """The existing bounded pointer/callback discovery, with overlap guards."""
    base = pe.OPTIONAL_HEADER.ImageBase
    sections = [s for s in pe.sections if s.Characteristics & 0x20000000]
    def executable(va):
        return any(base+s.VirtualAddress <= va < base+s.VirtualAddress+min(s.Misc_VirtualSize, s.SizeOfRawData) for s in sections)
    known = {i['address']: i['size'] for i in graph['instructions']}
    ordered = sorted(known)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    result = {}
    def offer(va, source):
        if va in known or va in seeds or va in result or not executable(va):
            return
        at = bisect.bisect_right(ordered, va)-1
        if at >= 0 and ordered[at]+known[ordered[at]] > va:
            return
        first = next(decoder.disasm(pe.get_data(va-base, 16), va, count=1), None)
        if first and first.mnemonic not in ('int3', 'hlt', 'ud2'):
            result[va] = source
    for section in pe.sections:
        if section.Name.rstrip(b'\0') not in (b'.rdata', b'.data'):
            continue
        raw = section.get_data()
        strings = bytearray(len(raw))
        for pattern in (rb'[\x20-\x7e]{3,}\x00', rb'(?:[\x00-\x7f]\x00){4,}'):
            for match in re.finditer(pattern, raw):
                strings[match.start():match.end()] = b'\1'*(match.end()-match.start())
        for library in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
            for item in library.imports:
                if item.name:
                    at = pe.get_offset_from_rva(item.hint_name_table_rva)-section.PointerToRawData
                    if 0 <= at < len(raw):
                        end = min(len(raw), at+2+len(item.name)+1)
                        strings[at:end] = b'\1'*(end-at)
        for offset in range(0, len(raw)-3, 4):
            if not any(strings[offset:offset+4]):
                offer(struct.unpack_from('<I', raw, offset)[0], f'data:0x{base+section.VirtualAddress+offset:x}')
    for instruction in graph['instructions']:
        if instruction['mnemonic'] in ('mov', 'push', 'lea'):
            for op in instruction['operands']:
                if op['kind'] == 'immediate' and not op.get('relative'):
                    offer(int(op['value_hex'], 16), f'callback:0x{instruction["address"]:x}')
        if instruction['mnemonic'] != 'jmp' or not instruction['operands']:
            continue
        op = instruction['operands'][0]
        if op.get('kind') != 'memory' or op.get('base') != 'none' or op.get('index') == 'none' or op.get('scale') != 4:
            continue
        table = op['displacement'] & 0xffffffff
        for index in range(4096):
            try:
                value = pe.get_dword_at_rva(table-base+index*4)
            except pefile.PEFormatError:
                break
            if value is None or not executable(value):
                break
            offer(value, f'jump-table:0x{table:x}+{index*4}')
    return result


def prepare(exe, tool):
    profile = json.loads((ROOT/'config/special_contents.json').read_text())
    rules = json.loads((ROOT/'config/porting_contract.json').read_text())['rules']
    exceptions = rules.get('source_executable_analysis_exceptions', [])
    if not any(e['program'] == profile['id'] and e['filename'] == profile['filename'] and
               e['sha256'] == profile['sha256'] and e['static_analysis_only'] for e in exceptions):
        raise ValueError('Special Contents static analysis has no scoped authorization')
    output = ROOT/'generated/special-contents'
    output.mkdir(parents=True, exist_ok=True)
    receipt_path = output/'preparation.json'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        if (receipt['input_sha256'] == profile['sha256'] and receipt['tool_sha256'] == digest(tool)
                and all(digest(ROOT/name) == sha for name, sha in receipt['cache_sha256'].items())):
            print('Reusing verified Special C++/CFG/image cache; no source executable opened.', flush=True)
            return receipt
        raise ValueError('Special cache changed; inspect it before an explicit fresh preparation')
    if exe is None:
        raise ValueError('First preparation requires --exe pointing to the authorized WA2_Special_cn.exe')
    exe = exe.resolve()
    if exe.name != profile['filename'] or digest(exe) != profile['sha256']:
        raise ValueError('Special Contents input does not match its authorized SHA-bound profile')
    data = exe.read_bytes()
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.Magic != 0x10b:
        raise ValueError('Special Contents expects i386 PE32')
    flags = subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    def run(mode, destination, seeds):
        command = [str(tool), mode, str(exe), str(destination)]
        for seed in sorted(seeds):
            command += ['--seed', hex(seed)]
        subprocess.run(command, cwd=ROOT, creationflags=flags, check=True, timeout=300)
    cfg = output/'cfg.json'
    seeds = {}
    run('cfg', cfg, seeds)
    history = []
    for round_number in range(7):
        graph = json.loads(cfg.read_text())
        if graph['diagnostics'] or graph.get('budget_exhausted'):
            raise ValueError('Special discovery diagnostics: '+repr(graph['diagnostics'][:20]))
        new = candidates(pe, graph, seeds)
        history.append(dict(round=round_number, instructions=len(graph['instructions']), seeds=len(seeds), new_seeds=len(new)))
        print(json.dumps(history[-1]), flush=True)
        if not new:
            break
        if round_number == 6 or len(seeds)+len(new) > 1500:
            raise ValueError('Special callback discovery did not converge within its bound')
        seeds.update(new)
        run('cfg', cfg, seeds)
    coverage = output/'coverage.json'
    run('coverage', coverage, seeds)
    coverage_data = json.loads(coverage.read_text())
    if coverage_data['unsupported']:
        raise ValueError('Unsupported Special instruction forms: '+repr(coverage_data['unsupported']))
    raw = output/'raw'
    run('project', raw, seeds)
    # Raw file-layout bytes are data for initial memory, never host instructions.
    (output/'image.bin').write_bytes(data)
    cache = [cfg, coverage, output/'image.bin', *sorted(raw.glob('code_page_*.cpp'))]
    receipt = dict(schema='wa2.special-preparation.v1', input_sha256=profile['sha256'],
        tool_sha256=digest(tool), seeds=sorted(seeds), discovery_history=history,
        cache_sha256={p.relative_to(ROOT).as_posix(): digest(p) for p in cache},
        original_main_executable_opened=False, source_executable_executed=False,
        game_launched=False, screen_access=False, whole_program_complete=False)
    write_json(receipt_path, receipt)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--tool', type=Path, default=ROOT/'build/special-toolchain/winrecomp.exe')
    args = parser.parse_args()
    receipt = prepare(args.exe, args.tool.resolve())
    print('Special cache ready:', receipt['input_sha256'], flush=True)


if __name__ == '__main__':
    main()
