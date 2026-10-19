"""Keep Zig's CRT/library selection and explicitly apply COFF /OPT:REF.

Pinned Zig 0.15.2 emits -DEBUG, but does not forward --gc-sections to COFF LLD:
https://github.com/ziglang/zig/blob/0.15.2/src/link/Lld.zig
Its verbose argv has no quoting. This launcher therefore checks every file
argument before replaying the exact linker inputs. It never invokes a shell.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    command = sys.argv[1:]
    if len(command) < 2 or command[1] != 'c++':
        raise ValueError('Expected a Zig C++ link command')
    compiler = Path(command[0]).resolve()
    version = subprocess.check_output([str(compiler), 'version'], text=True).strip()
    if version != '0.15.2':
        raise ValueError('Recheck the COFF release launcher for this compiler version')
    first = subprocess.run([*command, '-v'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    transcript = first.stdout.decode('utf-8', errors='replace')
    if first.returncode:
        sys.stdout.write(transcript); return first.returncode
    invocations = [line for line in transcript.splitlines() if line.startswith('lld-link ')]
    if len(invocations) != 1:
        raise ValueError('Expected exactly one resolved COFF linker invocation')
    arguments = invocations[0].split()[1:]
    # Zig's dump_argv prints unescaped arguments. Refuse an ambiguous path
    # instead of guessing, dropping inputs, or executing a reconstructed shell.
    for arg in arguments:
        if not arg.startswith('-') and not Path(arg).is_file():
            raise ValueError('Cannot replay an unescaped linker input: '+arg)
    def option(name):
        values = [arg[len(name):] for arg in arguments if arg.lower().startswith(name.lower())]
        if len(values) != 1: raise ValueError('Expected one '+name+' linker option')
        return Path(values[0]).resolve()
    output = option('-out:'); pdb = option('-pdb:')
    if not output.is_relative_to(ROOT/'build') or not pdb.is_relative_to(ROOT/'build'):
        raise ValueError('Release link outputs must stay in the build directory')
    if any(arg.lower().startswith('-opt:') for arg in arguments):
        raise ValueError('Unexpected optimization flags: recheck the compiler integration')
    uncollected_bytes = output.stat().st_size
    collected = [*arguments, '-opt:ref']
    completed = subprocess.run([str(compiler), 'lld-link', *collected])
    if completed.returncode: return completed.returncode
    proof = dict(schema='wa2.release-pe-link.v1', program=str(output), output_sha256=digest(output),
                 output_bytes=output.stat().st_size, uncollected_bytes=uncollected_bytes,
                 bytes_removed=uncollected_bytes-output.stat().st_size,
                 pdb=str(pdb), pdb_sha256=digest(pdb),
                 compiler=str(compiler), compiler_sha256=digest(compiler), compiler_version=version,
                 coff_opt_ref=True, shell=False, linker_argv=collected)
    output.with_suffix('.link.json').write_text(json.dumps(proof, indent=2)+'\n')
    output.with_suffix('.link.log').write_text(transcript)
    print(f'COFF release: removed {proof["bytes_removed"]:,} unreferenced bytes; matching PDB retained')
    return 0


if __name__ == '__main__':
    sys.exit(main())
