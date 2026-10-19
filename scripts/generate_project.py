"""Generate a SHA-bound x64 project; refuse unsupported instruction forms."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'generated'/'wa2-win64')
    parser.add_argument('--profile',type=Path,default=ROOT/'config'/'wa2.json')
    parser.add_argument('--tool',type=Path,default=ROOT/'build'/'winrecomp'/'winrecomp.exe')
    args=parser.parse_args()
    exe=ROOT/'WA2_chs.exe';tool=args.tool.resolve()
    profile=json.loads(args.profile.read_text())
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=profile['sha256']:raise ValueError('Input does not match the target profile')
    seeds=[]
    for seed in profile['seeds']:seeds+=['--seed',hex(seed)]
    subprocess.run([str(tool),'coverage',str(exe),str(ROOT/'logs'/'coverage.json'),*seeds],check=True)
    coverage=json.loads((ROOT/'logs'/'coverage.json').read_text())
    print(json.dumps(coverage,indent=2),flush=True)
    if coverage['unsupported']:raise RuntimeError('Unsupported forms remain; refusing a partial/trap-based build')
    subprocess.run([str(tool),'cfg',str(exe),str(ROOT/'logs'/'cfg.json'),*seeds],check=True)
    subprocess.run([str(tool),'project',str(exe),str(args.out),*seeds],check=True)
    cmake=args.out/'CMakeLists.txt'
    cmake.write_text(cmake.read_text().replace('LANGUAGES C CXX)','LANGUAGES C CXX ASM)')+f'\nadd_subdirectory("{(ROOT/"port").as_posix()}" wa2_port)\ntarget_link_libraries(recompiled_program PRIVATE wa2_port)\nset_target_properties(recompiled_program PROPERTIES OUTPUT_NAME WA2_SDL2_x64 RUNTIME_OUTPUT_DIRECTORY "${{CMAKE_BINARY_DIR}}/candidate")\n')
    main_file=args.out/'main.cpp'
    main_file.write_text('#include "'+(ROOT/'port'/'embedded_main.hpp').as_posix()+'"\n')
    (args.out/'wa2-profile.json').write_text(json.dumps(profile,indent=2)+'\n')

if __name__=='__main__':main()
