"""Recover verified codec callback records reached during BGM decoding."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import pefile
source=ROOT/'WA2_chs.exe';profile=json.loads((ROOT/'config/wa2.json').read_text())
if hashlib.sha256(source.read_bytes()).hexdigest()!=profile['sha256']:raise ValueError('Wrong target image')
pe=pefile.PE(str(source));targets=set()
# Adjacent callback records in .data; non-function fields are null. Entries
# point to validated C function prologues in the same codec as 0x470070.
for at in [*range(0x4d0b68,0x4d0be0,4),*range(0x4d0fe0,0x4d101c,4)]:
    value=struct.unpack('<I',pe.get_data(at-pe.OPTIONAL_HEADER.ImageBase,4))[0]
    if value==0:continue
    if not 0x46f000<=value<0x474000:raise ValueError(f'Unexpected codec table value {value:#x}')
    targets.add(value)
subprocess.run([sys.executable,str(ROOT/'scripts/add_observed_seed.py'),'--source','verified codec callback bundles at .data 0x4d0b68..0x4d0bdc and 0x4d0fe0..0x4d1018; observed entries 0x470070 and 0x473230 during BGM decoding',*[hex(value) for value in sorted(targets)]],check=True)
