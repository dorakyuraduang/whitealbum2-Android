"""Copy the already cached CRT constant table into common native C++ data.

Only cfg.json metadata and the cached data-only C++ section are read. No
source executable, resource archive, disassembler or game is involved.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def main():
    metadata = json.loads((ROOT/'logs/cfg.json').read_text(encoding='utf-8'))['image']
    section = next(s for s in metadata['sections'] if s['name'] == '.rdata')
    base = metadata['image_base'] + section['rva']
    cached = bytes(map(int, (ROOT/'generated/static-data/rdata.inc').read_text(
        encoding='utf-8').replace('\n', '').strip(',\r ').split(',')))
    cosine = cached[0x4a84d0-base:0x4a8d40-base]
    sine = cached[0x4ac710-base:0x4acf80-base]
    if len(cosine) != 2160 or cosine != sine:
        raise ValueError('The two cached CRT tables no longer have the admitted layout')
    words = struct.unpack('<270Q', cosine)
    text = ('// Generated from cached static-data/rdata.inc; never from an executable.\n'
            '// SHA256 of the 2160 data bytes: '+hashlib.sha256(cosine).hexdigest()+'\n'
            '#pragma once\n#include <array>\n#include <cstdint>\n'
            'namespace wa2::math::detail {\n'
            'inline constexpr std::array<std::uint64_t,270> crt_trigonometry_words{{\n')
    text += ''.join('    '+','.join(f'0x{v:016x}ull' for v in words[n:n+4])+',\n'
                    for n in range(0, len(words), 4))
    text += '}};\n}\n'
    output = ROOT/'engine/trigonometry_tables.hpp'
    if not output.exists() or output.read_text(encoding='utf-8') != text:
        output.write_text(text, encoding='utf-8', newline='\n')
    print('PASS two identical cached CRT tables; 270 shared binary64 words')


if __name__ == '__main__':
    main()
