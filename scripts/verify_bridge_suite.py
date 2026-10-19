"""Run the target bridge regressions against the currently built libraries."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    directory=ROOT/'build/wa2-win64/wa2_port';image=str(ROOT/'WA2_chs.exe');results=[]
    subprocess.run([sys.executable,str(ROOT/'tests/make_media_fixture.py'),str(ROOT/'build/media-tests/sample.avi')],check=True,cwd=ROOT)
    for name,args in [
        ('wa2_port_tests',[image,str(ROOT/'logs/api-coverage.json')]),
        ('wa2_script_tests',[image,str(ROOT/'logs/script-handler-tests.json')]),
        ('wa2_fast_tests',[image,str(ROOT/'logs/static-batch-tests.json')]),
        ('wa2_block_tests',[image,str(ROOT/'logs/static-block-tests.json')]),
        ('wa2_vorbis_tests',[image,str(ROOT/'logs/vorbis-batch-tests.json')]),
        ('wa2_vorbis_butterfly_tests',[image,str(ROOT/'logs/vorbis-butterfly-tests.json')]),
        ('wa2_color_tests',[image,str(ROOT/'logs/color-batch-tests.json')]),
        ('wa2_vertex_tests',[image,str(ROOT/'logs/vertex-batch-tests.json')]),
        ('wa2_uv_tests',[image,str(ROOT/'logs/uv-batch-tests.json')]),
        ('wa2_fp_sequence_tests',[image,str(ROOT/'logs/fp-sequence-tests.json')]),
        ('wa2_shift_tests',[str(ROOT/'logs/shift-fast-tests.json')]),
        ('wa2_sprite_transform_tests',[image,str(ROOT/'logs/sprite-transform-tests.json')]),
        ('wa2_bitreader_tests',[image,str(ROOT/'logs/bitreader-batch-tests.json')]),
        ('wa2_graphics_tests',[image]),
        ('wa2_media_tests',[image,str(ROOT/'build/media-tests/sample.avi')]),
        ('wa2_thread_tests',[image]),
        ('wa2_win64_tests',[image,str(ROOT/'logs/win64-bridge-tests.json')]),
    ]:
        program=directory/(name+'.exe');log=ROOT/'logs'/(name+'.log')
        with log.open('wb') as console:result=subprocess.run([str(program),*args],cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,timeout=90)
        print(name,result.returncode,flush=True)
        if result.returncode:raise RuntimeError(log.read_text(encoding='utf-8',errors='replace')[-5000:])
        results.append({'fixture':name,'return_code':0,'program_sha256':digest(program),'log':str(log),'log_sha256':digest(log)})
    # A hidden real console supplies CONIN$/CONOUT$ handles, as double-clicking
    # run.cmd does. Omit redirected stdio so CreateProcess supplies those handles.
    startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    program=directory/'wa2_win64_tests.exe';report=ROOT/'logs/win64-console-bridge-tests.json'
    process=subprocess.Popen([str(program),image,str(report)],cwd=ROOT,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup)
    try:code=process.wait(timeout=90)
    except subprocess.TimeoutExpired:process.kill();process.wait();raise
    if code:raise RuntimeError('Native-console Win64 fixture failed: '+str(code))
    print('wa2_win64_tests with native console',code,flush=True)
    results.append({'fixture':'wa2_win64_tests with native console','return_code':code,'program_sha256':digest(program),'report':str(report),'report_sha256':digest(report)})
    subprocess.run([sys.executable,str(ROOT/'scripts/verify_embedded_launch.py')],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    output=ROOT/'out/WA2_SDL2_x64.exe'
    proof={'schema':'wa2.bridge-suite.v1','output_sha256':digest(output),'port_library_sha256':digest(directory/'libwa2_port.a'),
           'runtime_library_sha256':digest(ROOT/'build/wa2-win64/winrecomp/libwinrecomp_guest.a'),
           'core_tests_log':str(ROOT/'logs/win64-toolchain-tests.log'),'fixtures':results,'full_api_conformance':False}
    assert '100% tests passed out of 12' in (ROOT/'logs/win64-toolchain-tests.log').read_text()
    (ROOT/'logs/bridge-suite.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('All target bridge fixtures passed.',flush=True)

if __name__=='__main__':main()
