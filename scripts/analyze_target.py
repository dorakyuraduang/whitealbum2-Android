"""Record the PE/ABI contract without running the original executable."""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "python"))
import capstone
import pefile


def analyze(path: Path) -> dict:
    data = path.read_bytes()
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise ValueError("This project expects an i386 PE32 executable")
    base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    entry_rva = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    imports = []
    for library in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        imports.append({
            "dll": library.dll.decode("ascii"),
            "functions": [{
                "name": item.name.decode("ascii") if item.name else f"#{item.ordinal}",
                "iat_va": item.address,
            } for item in library.imports],
        })
    report = {
        "schema": "wa2.target.v1",
        "path": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
        "guest": "x86 Windows PE32",
        "host": "x86_64 Windows PE32+",
        "image_base": base,
        "image_size": pe.OPTIONAL_HEADER.SizeOfImage,
        "entry_va": base + entry_rva,
        "coff_symbols": pe.FILE_HEADER.NumberOfSymbols,
        "relocation_size": pe.OPTIONAL_HEADER.DATA_DIRECTORY[5].Size,
        "tls_size": pe.OPTIONAL_HEADER.DATA_DIRECTORY[9].Size,
        "clr_size": pe.OPTIONAL_HEADER.DATA_DIRECTORY[14].Size,
        "sections": [{
            "name": s.Name.rstrip(b"\0").decode("ascii"),
            "va": base + s.VirtualAddress,
            "virtual_size": s.Misc_VirtualSize,
            "raw_size": s.SizeOfRawData,
            "characteristics": s.Characteristics,
        } for s in pe.sections],
        "imports": imports,
        "imported_dll_count": len(imports),
        "imported_function_count": sum(len(i["functions"]) for i in imports),
        "entry_preview": [{"va": i.address, "text": i.mnemonic + " " + i.op_str}
                          for i in decoder.disasm(pe.get_data(entry_rva, 16), base + entry_rva)],
        "notes": [
            "The original EXE has not been launched by this analyzer.",
            "Compiled guest code retains 32-bit addresses and CPU state on the x64 host.",
            "Game assets are outside the current build/verification scope.",
        ],
    }
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path, nargs="?", default=ROOT / "WA2_chs.exe")
    parser.add_argument("--out", type=Path, default=ROOT / "logs" / "target.json")
    args = parser.parse_args()
    report = analyze(args.exe)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ["sha256", "guest", "host", "entry_va", "imported_dll_count", "imported_function_count"]}, indent=2))
