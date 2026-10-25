"""Same memory-check objects linked to verified previous/current shared AOT."""
import argparse,ctypes,hashlib,json,os,pathlib,shlex,statistics,subprocess
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline-dir',type=pathlib.Path,required=True);parser.add_argument('--baseline-release',required=True);args=parser.parse_args()
r=pathlib.Path(__file__).resolve().parents[1];b=r/'build/shared-aot-x64';folder=args.baseline_dir.resolve()
if not folder.is_relative_to(r/'build'):raise ValueError('Baseline directory must be inside this workspace build directory')
k=ctypes.WinDLL('kernel32');k.GetCurrentProcess.restype=ctypes.c_void_p;k.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];assert k.SetPriorityClass(k.GetCurrentProcess(),0x4000);k.SetErrorMode(0x8003)
flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
env=os.environ.copy();env.update(CMAKE_BUILD_PARALLEL_LEVEL='1',TEMP=str(r/'build/temp'),TMP=str(r/'build/temp'),ZIG_GLOBAL_CACHE_DIR=str(r/'build/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(r/'build/zig-local'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
previous=json.loads((folder/'before-shared-aot-verification.json').read_bytes());assert previous['passed'] and previous['source_sha256']['recompiled/native_sprites.cpp']==sha(folder/'native_sprites.cpp')
ninja=(b/'build.ninja').read_text();results={}
with (folder/'comparison.log').open('wb') as log:
    for label,target,source in [('dialogue','wa2_dialogue_cpu_tests','dialogue_cpu'),('sprites','wa2_native_sprite_cpu_tests','native_sprite_cpu')]:
        objects=list((b/('CMakeFiles/'+target+'.dir')).rglob(source+'.cpp.obj'));assert len(objects)==1
        obj=objects[0];old=folder/(label+'-same-object-before.exe');current=b/(target+'.exe')
        block=ninja.split('build '+target+'.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0]
        libs=shlex.split(block);assert libs.count('libwa2_shared_aot.a')==1;libs[libs.index('libwa2_shared_aot.a')]=str(folder/'libwa2_shared_aot.a')
        subprocess.run([str(r/'tools/python/ziglang/zig.exe'),'c++','-target','x86_64-windows-gnu','-Os',str(obj),'-o',str(old),*libs],cwd=b,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
        observations=[]
        for pair in range(5):
            values=[None,None]
            for order in range(2):
                side=(pair+order)&1;report=folder/(label+'-'+str(pair)+'-'+str(side)+'.json')
                subprocess.run([str(current if side else old),str(report)],cwd=r,env=env,creationflags=flags,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
                values[side]=json.loads(report.read_bytes());assert values[side]['passed']
            observations.append(values)
        keys=['exact_source_variants','accepted_glyphs','legacy_api_calls'] if label=='dialogue' else ['cases','decline_cases','visible_grid_cases','tiles']
        for values in observations:
            a=values[0]['native_glyph_chain'] if label=='dialogue' else values[0];c=values[1]['native_glyph_chain'] if label=='dialogue' else values[1]
            assert all(a[key]==c[key] for key in keys)
        components=[]
        for component in range(1 if label=='dialogue' else 3):
            pairs=[[statistics.median(pair[1] for pair in (round[side]['native_glyph_chain']['paired_ns'] if label=='dialogue' else round[side]['timings'][component]['paired_ns'])) for side in range(2)] for round in observations]
            components.append(dict(kind=component,paired_ns=pairs,median_paired_speedup=statistics.median(a/b for a,b in pairs),before_median_ms=statistics.median(a for a,b in pairs)/1e6,after_median_ms=statistics.median(b for a,b in pairs)/1e6))
        if label=='dialogue':
            for mode,field in [('static_glyph_geometry','paired_ns'),('static_geometry_with_origin_changes','noisy_paired_ns')]:
                pairs=[[statistics.median(pair[1] for pair in round[side][field]) for side in range(2)] for round in observations]
                components.append(dict(kind=mode,paired_ns=pairs,median_paired_speedup=statistics.median(a/b for a,b in pairs),before_median_ms=statistics.median(a for a,b in pairs)/1e6,after_median_ms=statistics.median(b for a,b in pairs)/1e6))
            pairs=[[statistics.median(round[side]['native_glyph_chain']['changing_native_ns']) for side in range(2)] for round in observations]
            components.append(dict(kind='changing_lines',paired_ns=pairs,median_paired_speedup=statistics.median(a/b for a,b in pairs),before_median_ms=statistics.median(a for a,b in pairs)/1e6,after_median_ms=statistics.median(b for a,b in pairs)/1e6))
        results[label]=dict(same_test_object_sha256=sha(obj),test_source_sha256=sha(r/('tests/'+source+'.cpp')),before_binary_sha256=sha(old),after_binary_sha256=sha(current),components=components,observations=observations)
release=json.loads((r/'config/release.json').read_bytes())['name'];version=release.removesuffix('-shared-aot')
tracked=['scripts/compare_glyph_cpu.py','recompiled/native_sprites.cpp','recompiled/geometry_cache_detail.hpp','engine/glyph_index.hpp','engine/byte_equality.hpp','engine/glyph_geometry_memo.hpp','engine/single_tile_geometry.hpp','platform/render/submission_cache.hpp','tests/glyph_index.cpp','tests/glyph_geometry.cpp','tests/byte_equality.cpp','tests/geometry_cache_snapshots.hpp','tests/native_sprite_cpu.cpp','tests/dialogue_cpu.cpp']
result=dict(schema='wa2.glyph-extreme-comparison.v1',baseline_release=args.baseline_release,after_release=release,baseline_library_sha256=sha(folder/'libwa2_shared_aot.a'),baseline_verification_sha256=sha(folder/'before-shared-aot-verification.json'),source_sha256={name:sha(r/name) for name in tracked},components=results,game_launched=False,screen_access=False,whole_fastforward_measured=False,device_fps_measured=False,maximum_jobs=1,passed=True)
(r/('logs/performance-glyph-extreme-'+version+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({name:item['components'] for name,item in results.items()},indent=2),flush=True)
