"""Recover the bounded CRT memcpy/memmove dispatch tables beside 0x475824."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
source=ROOT/'WA2_chs.exe';profile=json.loads((ROOT/'config/wa2.json').read_text())
if hashlib.sha256(source.read_bytes()).hexdigest()!=profile['sha256']:raise ValueError('Wrong target image')
pe=pefile.PE(str(source));targets=set()
# Index domains follow the AND 3, CMP 8 and NEG ECX instructions in this
# function. Alignment slot zero is intentionally unreachable and contains code.
for table,count in [(0x4757e8,4),(0x475868,8),(0x4758d4,4),(0x475974,4),(0x475a04,8),(0x475a70,4)]:
    for value in struct.unpack('<'+'I'*count,pe.get_data(table-pe.OPTIONAL_HEADER.ImageBase,count*4)):
        if 0x475750<=value<0x475ac0:targets.add(value)
subprocess.run([sys.executable,str(ROOT/'scripts/add_observed_seed.py'),'--source','verified CRT memcpy/memmove alignment, unroll and tail dispatch tables near observed 0x475824',*[hex(value) for value in sorted(targets)]],check=True)
