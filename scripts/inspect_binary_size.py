"""Attribute linked PE bytes using its matching PDB section contributions.

Read-only analysis of files we built. Format references:
https://llvm.org/docs/PDB/MsfFile.html
https://llvm.org/docs/PDB/DbiStream.html
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/python'))
import pefile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pdb_streams(path):
    data = path.read_bytes()
    if data[:32] != b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0':
        raise ValueError('Expected an MSF 7 PDB')
    block_size, _, blocks, directory_size, _, block_map = struct.unpack_from('<6I', data, 32)
    assert block_size in [512, 1024, 2048, 4096] and len(data) == blocks*block_size
    def read_blocks(indices, size):
        assert all(index < blocks for index in indices)
        return b''.join(data[index*block_size:(index+1)*block_size] for index in indices)[:size]
    count = (directory_size+block_size-1)//block_size
    directory_blocks = struct.unpack_from(f'<{count}I', data, block_map*block_size)
    directory = read_blocks(directory_blocks, directory_size)
    count = struct.unpack_from('<I', directory)[0]
    sizes = struct.unpack_from(f'<{count}I', directory, 4)
    result = []; at = 4+count*4
    for size in sizes:
        if size == 0xffffffff:
            result.append(b''); continue
        count = (size+block_size-1)//block_size
        indices = struct.unpack_from(f'<{count}I', directory, at); at += count*4
        result.append(read_blocks(indices, size))
    assert at == len(directory)
    return result


def inspect(program, pdb):
    pe = pefile.PE(str(program)); streams = pdb_streams(pdb)
    debug = next(entry.struct for entry in pe.DIRECTORY_ENTRY_DEBUG if entry.struct.Type == 2)
    codeview = pe.__data__[debug.PointerToRawData:debug.PointerToRawData+debug.SizeOfData]
    assert codeview[:4] == b'RSDS' and codeview[4:20] == streams[1][12:28]
    assert struct.unpack_from('<I', codeview, 20)[0] == struct.unpack_from('<I', streams[1], 8)[0]
    dbi = streams[3]
    module_size, contribution_size = struct.unpack_from('<ii', dbi, 24)
    modules = []; at = 64
    while at < 64+module_size:
        end = dbi.index(0, at+64); module = dbi[at+64:end].decode('utf-8')
        next_end = dbi.index(0, end+1); archive = dbi[end+1:next_end].decode('utf-8')
        modules.append(dict(module=module, archive=archive)); at = (next_end+4)&~3
    assert at == 64+module_size
    version = struct.unpack_from('<I', dbi, at)[0]
    stride = {0xeffe0000+19970605:28, 0xeffe0000+20140516:32}[version]
    assert (contribution_size-4) % stride == 0
    rows = []; section_totals = collections.Counter(); contributions = []
    for offset in range(at+4, at+contribution_size, stride):
        section, start, size, flags, module = struct.unpack_from('<H2xiiIH', dbi, offset)
        assert 1 <= section <= len(pe.sections) and start >= 0 and size >= 0 and module < len(modules)
        assert start+size <= pe.sections[section-1].Misc_VirtualSize
        contributions.append((section, start, start+size, module))
    # ICF aliases can refer to the same range; count each physical byte once.
    per_module = collections.Counter(); per_section_module = collections.Counter(); mapped_bytes = collections.Counter(); aliases = 0
    end_by_section = collections.defaultdict(int)
    for section, start, end, module in sorted(contributions):
        previous = end_by_section[section]; mapped = max(0, end-max(start, previous))
        count = max(0, min(end, pe.sections[section-1].SizeOfRawData)-max(start, previous))
        aliases += end-start-mapped; end_by_section[section] = max(previous, end); mapped_bytes[module] += mapped
        per_module[module] += count; per_section_module[module, section] += count; section_totals[section] += count
    for module, size in per_module.items():
        rows.append(dict(**modules[module], bytes=size, mapped_bytes=mapped_bytes[module],
                         sections={pe.sections[s-1].Name.rstrip(b'\0').decode():n
                                   for (m,s),n in per_section_module.items() if m == module and n}))
    sections = [dict(name=s.Name.rstrip(b'\0').decode(), raw_bytes=s.SizeOfRawData,
                     virtual_bytes=s.Misc_VirtualSize, attributed_bytes=section_totals[n+1])
                for n,s in enumerate(pe.sections)]
    return dict(schema='wa2.binary-size.v1', program=dict(file=str(program.resolve()),sha256=digest(program),bytes=program.stat().st_size),
                pdb=dict(file=str(pdb.resolve()),sha256=digest(pdb),guid_matches=True), sections=sections,
                modules=sorted(rows,key=lambda row:row['bytes'],reverse=True),
                duplicate_contribution_bytes=aliases, runtime_launched=False, screen_access=False)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program',type=Path,required=True); parser.add_argument('--pdb',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True); args=parser.parse_args()
    result=inspect(args.program,args.pdb); args.report.write_text(json.dumps(result,indent=2)+'\n')
    for row in result['modules'][:18]:
        print(f'{row["bytes"]:10d}  {row["module"]}  {row["archive"]}')
