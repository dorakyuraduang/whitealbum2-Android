"""Compare finite text fixtures with a retained passing shared AOT archive.

One hidden, below-normal job on one core. No game, original resources, window,
device, screen or original executable. Component CPU times are not phone FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
from build_pak_tool import low_priority

ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-dir',required=True,type=Path)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/shared-aot-refresh-x64')
    args=parser.parse_args();low_priority();folder=args.baseline_dir.resolve();build=args.build_dir.resolve()
    if not folder.is_relative_to(ROOT/'build'):raise ValueError('Retained private baseline must be inside build/')
    old=folder/'libwa2_shared_aot.a';proof=json.loads((folder/'before-verification.json').read_text())
    if not proof['passed'] or proof['windows_library']['sha256']!=digest(old):raise ValueError('Baseline archive is not verified')
    obj=build/'CMakeFiles/wa2_refresh_novel_cpu.dir/D_/wa2/tests/novel_cpu.cpp.obj'
    if not obj.exists():raise ValueError('Build the current finite novel_cpu fixture first')
    block=(build/'build.ninja').read_text().split('build wa2_refresh_novel_cpu.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0]
    libraries=shlex.split(block)
    if libraries.count('shared_aot/libwa2_shared_aot.a')!=1:raise ValueError('Unexpected common archive link')
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local'))
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    binaries=[];observations=[];snapshot_sha=None
    with (folder/'text-comparison.log').open('wb') as log:
        def run(command,cwd=build):
            subprocess.run(list(map(str,command)),cwd=cwd,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        for side in range(2):
            inputs=libraries.copy()
            if not side:inputs[inputs.index('shared_aot/libwa2_shared_aot.a')]=str(old)
            binary=folder/('same-text-object-'+str(side)+'.exe');binaries.append(binary)
            run([ROOT/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',obj,'-o',binary,*inputs])
        for pair in range(3):
            reports=[None,None];snapshots=[None,None]
            for order in range(2):
                side=(pair+order)&1;path=folder/('text-pair-'+str(pair)+'-'+str(side)+'.json');run([binaries[side],path],ROOT)
                reports[side]=json.loads(path.read_text());snapshots[side]=digest(Path(str(path)+'.bin'))
                if not reports[side]['passed']:raise ValueError('Text fixture failed')
            if snapshots[0]!=snapshots[1] or (snapshot_sha and snapshot_sha!=snapshots[0]):raise ValueError('Changed text output/public ABI snapshot')
            snapshot_sha=snapshots[0]
            clean=lambda report:[{k:v for k,v in page.items() if not k.endswith('_ns') and k not in ('local_jumps','local_yields')} for page in report['pages']]
            if clean(reports[0])!=clean(reports[1]):raise ValueError('Changed text/glyph output counters')
            observations.append(reports)
    results=[]
    for page in range(len(observations[0][0]['pages'])):
        before=[r[0]['pages'][page]['native_ns'] for r in observations];after=[r[1]['pages'][page]['native_ns'] for r in observations]
        results.append(dict(characters=observations[0][0]['pages'][page]['characters'],two_byte=observations[0][0]['pages'][page]['two_byte'],formatted=observations[0][0]['pages'][page]['formatted'],before_ns=before,after_ns=after,median_before_ns=statistics.median(before),median_after_ns=statistics.median(after)))
    report=dict(schema='wa2.text-flow-component-comparison.v1',same_test_object_sha256=digest(obj),fixture_source_sha256=digest(ROOT/'tests/novel_cpu.cpp'),old_library_sha256=digest(old),new_library_sha256=digest(build/'shared_aot/libwa2_shared_aot.a'),binaries=[dict(file=str(p),sha256=digest(p)) for p in binaries],snapshot_sha256=snapshot_sha,results=results,game_launched=False,screen_access=False,source_executable_opened=False,phone_fps_measured=False,passed=True)
    (ROOT/'logs/text-flow-component-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS identical snapshots in three alternating same-object pairs; component CPU times recorded, no phone FPS claim.')
if __name__=='__main__':main()
