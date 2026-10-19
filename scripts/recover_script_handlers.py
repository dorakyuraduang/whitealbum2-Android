"""Recover the bounded script callback table, including pointers resembling text."""
from pathlib import Path
import bisect
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools/python'))
import pefile

TABLE = 0x4c25c0
COUNT = 128
GUARDS = {0x453f92: bytes.fromhex('3d80000000'),
          0x453fc9: bytes.fromhex('8b0c85c0254c00ffd1')}


def inspect():
    image = ROOT / 'WA2_chs.exe'
    profile = json.loads((ROOT / 'config/wa2.json').read_text())
    if hashlib.sha256(image.read_bytes()).hexdigest() != profile['sha256']:
        raise ValueError('Original image/profile mismatch')
    pe = pefile.PE(str(image))
    base = pe.OPTIONAL_HEADER.ImageBase
    for address, expected in GUARDS.items():
        if pe.get_data(address - base, len(expected)) != expected:
            raise ValueError('Script dispatcher instruction bytes changed')
    graph = json.loads((ROOT / 'logs/cfg.json').read_text())
    sizes = {item['address']: item['size'] for item in graph['instructions']}
    ordered = sorted(sizes)
    executable = [(base + section.VirtualAddress,
                   base + section.VirtualAddress + min(section.Misc_VirtualSize, section.SizeOfRawData))
                  for section in pe.sections if section.Characteristics & 0x20000000]
    handlers = []
    for index in range(COUNT):
        address = pe.get_dword_at_rva(TABLE - base + index * 4)
        if not address:
            continue  # Reserved script opcode 0x13 remains a null slot.
        if not any(start <= address < end for start, end in executable):
            raise ValueError('Script table contains a non-executable callback')
        previous = bisect.bisect_right(ordered, address) - 1
        if address not in sizes and previous >= 0 and ordered[previous] + sizes[ordered[previous]] > address:
            raise ValueError('Script callback overlaps an existing instruction')
        handlers.append({'opcode': index, 'pointer_address': TABLE + index * 4,
                         'entry': address, 'translated': address in sizes})
    return profile, handlers


def main():
    profile, handlers = inspect()
    missing = [handler['entry'] for handler in handlers if not handler['translated']]
    if missing:
        subprocess.run([sys.executable, str(ROOT / 'scripts/add_observed_seed.py'),
                        *[hex(address) for address in missing], '--source',
                        'bounded script callback table 0x4c25c0; opcode < 128 at 0x453f92; load/call at 0x453fc9/0x453fd0'],
                       cwd=ROOT, check=True)
    _, recovered = inspect()
    if not all(handler['translated'] for handler in recovered):
        raise ValueError('Script handlers remain untranslated')
    evidence = {'schema': 'wa2.script-handler-recovery.v1', 'input_sha256': profile['sha256'],
                'table': TABLE, 'slots': COUNT, 'null_slots': [0x13],
                'dispatcher_guards': {hex(address): code.hex() for address, code in GUARDS.items()},
                'added_entries': missing, 'handlers': recovered, 'all_non_null_handlers_translated': True}
    (ROOT / 'logs/script-handler-recovery.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps({'added_entries': len(missing), 'translated_handlers': len(recovered),
                      'all_non_null_handlers_translated': True}, indent=2))


if __name__ == '__main__':
    main()
