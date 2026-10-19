"""Read-only listing of static code around measured hot addresses."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/python'))
import capstone,pefile
image=pefile.PE(str(ROOT/'WA2_chs.exe'))
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for arg in sys.argv[1:]:
    start,length=arg.split(':');start=int(start,16);length=int(length,16)
    print(f'\nCode {start:#x}:')
    for instruction in decoder.disasm(image.get_data(start-image.OPTIONAL_HEADER.ImageBase,length),start):
        print(f'{instruction.address:#x} {instruction.bytes.hex():18} {instruction.mnemonic} {instruction.op_str}')
