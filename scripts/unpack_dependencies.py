"""Unpack downloaded, pinned decoder dependencies without invoking Git."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"


def unpack(archive: Path, destination: Path) -> dict:
    with zipfile.ZipFile(archive) as zf:
        prefix = zf.infolist()[0].filename.split("/", 1)[0] + "/"
        for info in zf.infolist():
            if not info.filename.startswith(prefix):
                raise ValueError(f"Unexpected archive root: {info.filename}")
            relative = Path(info.filename[len(prefix):])
            target = destination / relative
            if not target.resolve().is_relative_to(destination.resolve()):
                raise ValueError(f"Unsafe archive path: {info.filename}")
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                data = zf.read(info)
                if target.exists() and target.read_bytes() != data:
                    raise FileExistsError(f"Refusing to replace changed file: {target}")
                target.write_bytes(data)
    return {"archive": archive.name, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}


if __name__ == "__main__":
    decoder = TOOLS / "WindowsRecompTool-main" / "third_party" / "zydis"
    entries = [
        unpack(TOOLS / "zydis.zip", decoder),
        unpack(TOOLS / "zycore.zip", decoder / "dependencies" / "zycore"),
    ]
    for name in ["winrecomp.zip"]:
        path = TOOLS / name
        entries.append({"archive": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    (TOOLS / "archive-hashes.json").write_text(json.dumps(entries, indent=2) + "\n")
    print(json.dumps(entries, indent=2))
