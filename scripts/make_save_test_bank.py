"""Clone a complete original-format WA2 manual save into slots 00..99.

The generated saves and ZIP stay local. No game executable, game UI, device,
system save or launcher setting is accessed or changed.
"""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import zipfile


def low_priority():
    if os.name != 'nt':
        return
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    process = kernel.GetCurrentProcess()
    available, system = ctypes.c_size_t(), ctypes.c_size_t()
    if not kernel.SetPriorityClass(process, 0x4000):
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.GetProcessAffinityMask(process, ctypes.byref(available), ctypes.byref(system)):
        raise ctypes.WinError(ctypes.get_last_error())
    if not kernel.SetProcessAffinityMask(process, available.value & -available.value):
        raise ctypes.WinError(ctypes.get_last_error())


def sha(data):
    return hashlib.sha256(data).hexdigest()


def manual_save(source):
    # Reject executable/system/native-save inputs before reading their contents.
    if not re.fullmatch(r'save_(?:[0-9]{2}|[qQ])\.sav', source.name, re.IGNORECASE):
        raise ValueError('Choose an original manual save named save_XX.sav or save_Q.sav')
    before = source.stat()
    if not 88 + 65536 <= before.st_size <= 64 * 1024 * 1024:
        raise ValueError('Manual save size is outside supported bounds')
    with source.open('rb') as file:
        header = file.read(88)
        if len(header) != 88:
            raise ValueError('Truncated manual-save header')
        version, story = struct.unpack_from('<II', header)
        if version not in (1, 2) or story > 9999999:
            raise ValueError('Not a supported original-format manual save')
        year, month, weekday, day, hour, minute, second, millisecond = struct.unpack_from('<8H', header, 8)
        datetime.datetime(year, month, day, hour, minute, second)
        if weekday > 6 or millisecond > 999 or b'\0' not in header[24:88]:
            raise ValueError('Invalid manual-save date or description')
        data = header + file.read(64 * 1024 * 1024)
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or len(data) != before.st_size:
        raise ValueError('Source save changed during the read; try again')
    return data, version


def generate(source, output):
    source = source.resolve(strict=True)
    output = output.resolve()
    archive = output.with_name(output.name + '.zip')
    if output.exists() or archive.exists():
        raise FileExistsError('Choose a new output folder; existing files are never overwritten')
    data, version = manual_save(source)
    expected = sha(data)
    output.mkdir(parents=True, exist_ok=False)
    names = [f'save_{slot:02d}.sav' for slot in range(100)]
    for name in names:
        with (output / name).open('xb') as file:
            file.write(data)
    for name in names:
        if sha((output / name).read_bytes()) != expected:
            raise ValueError('Generated save failed its SHA-256 check: ' + name)
    if sorted(path.name for path in output.glob('*.sav')) != names:
        raise ValueError('The generated bank must contain exactly slots 00..99')
    if sha(source.read_bytes()) != expected:
        raise ValueError('Source save changed while creating the test bank')
    report = dict(schema='wa2.save-list-test-bank.v1', source_name=source.name,
                  version=version, count=100, first=names[0], last=names[-1],
                  bytes_each=len(data), total_save_bytes=len(data)*100, sha256_each=expected,
                  identical_story_point=True, source_unchanged=True,
                  per_file_sha256_verified=True, game_launched=False, screen_access=False,
                  whole_gameplay_verified=False)
    (output / 'CHECKSUMS.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (output / 'README.txt').write_text(
        'WA2 00-99 save-list test bank\n\n'
        'Contains exactly 100 complete saves: save_00.sav through save_99.sav.\n'
        'All slots clone the same existing save and restore the same story point.\n'
        'Headers, thumbnails and all payload bytes are preserved without editing.\n'
        'Back up your saves before copying these files into your active save folder.\n'
        'Use normal mode (Test Mode off), or deliberately choose the separate test-mode folder.\n'
        'No Sys.sav, launcher settings or game configuration is included.\n'
        'This fixture tests a full list; it does not itself fix a load-menu bug.\n', encoding='utf-8')
    # Exclusive creation also prevents overwriting an existing ZIP. On failure
    # the partial diagnostic output is retained rather than deleting user paths.
    with archive.open('xb') as file:
        with zipfile.ZipFile(file, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as package:
            for name in names:
                package.write(output / name, name)
            package.write(output / 'README.txt', 'README.txt')
            package.write(output / 'CHECKSUMS.json', 'CHECKSUMS.json')
    with zipfile.ZipFile(archive) as package:
        if package.testzip() is not None:
            raise ValueError('ZIP CRC verification failed')
        if sorted(name for name in package.namelist() if name.endswith('.sav')) != names:
            raise ValueError('ZIP save-slot inventory mismatch')
        for name in names:
            if sha(package.read(name)) != expected:
                raise ValueError('ZIP save failed its SHA-256 check: ' + name)
    return dict(**report, folder=str(output), zip=str(archive), zip_bytes=archive.stat().st_size,
                zip_sha256=sha(archive.read_bytes()), zip_verified=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='An existing complete original manual save')
    parser.add_argument('--output', type=Path, required=True, help='A new output folder; a sibling ZIP is also created')
    args = parser.parse_args()
    low_priority()
    print(json.dumps(generate(args.source, args.output), indent=2))


if __name__ == '__main__':
    main()
