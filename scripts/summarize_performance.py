"""Bind measured startup/loop results to the exact optimized executable."""
from pathlib import Path
import hashlib
import json

ROOT=Path(__file__).resolve().parent.parent

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    evidence={}
    for name in ['before','after']:
        path=ROOT/f'logs/performance-{name}.json'
        record=json.loads(path.read_text())
        state=record['report'].get('process',record['report'])
        graphics=state['direct3d9']
        if record['host_return_code'] or record['timed_out'] or record['report'].get('error'):raise ValueError(f'{name}: benchmark did not finish')
        if not state['diagnostic_stop'] or graphics['presents']!=200:raise ValueError(f'{name}: different frame cutoff')
        if state['input_sha256']!=digest(ROOT/'WA2_chs.exe'):raise ValueError('Different original image')
        if '--profile' not in record['command'] or '--snapshot' not in record['command']:raise ValueError('Different diagnostic options')
        evidence[name]={'output_sha256':record['output_sha256'],'evidence':str(path),'elapsed_seconds':record['elapsed_seconds'],'frame_times_seconds':{k:v/1000 for k,v in graphics['frame_times_ms'].items()},'presented_frames':graphics['presents'],'draw_calls':graphics['draw_calls'],'textures':graphics['textures_created'],'uploads':graphics['uploads'],'audio_uploads':state['directsound']['uploads'],'guest_instruction_bodies':state['native_instructions']}
    if evidence['after']['output_sha256']!=digest(ROOT/'out/WA2_SDL2_x64.exe'):raise ValueError('Timing is from another output build')
    if evidence['before']['output_sha256']!=digest(ROOT/'build/performance-baseline/WA2_profiled_x64.exe'):raise ValueError('Baseline identity mismatch')
    if evidence['before']['textures']!=evidence['after']['textures'] or evidence['before']['uploads']!=evidence['after']['uploads']:raise ValueError('Different loaded graphics resources')
    hotloop=json.loads((ROOT/'logs/performance-hotloop.json').read_text())
    if hotloop['optimized_memory_sha256']!=digest(ROOT/'tools/WindowsRecompTool-main/include/winrecomp/memory.hpp'):raise ValueError('Loop timing is from another memory implementation')
    if hotloop['translation_sha256']!=digest(ROOT/'generated/wa2-win64/code_page_1079.cpp'):raise ValueError('Loop translation changed')
    if not hotloop['cpu_fpu_and_all_mapped_writable_bytes_identical']:raise ValueError('Loop state differs')
    record={'schema':'wa2.performance-verification.v1','input_sha256':digest(ROOT/'WA2_chs.exe'),**evidence,'elapsed_time_reduction_percent':round(100*(1-evidence['after']['elapsed_seconds']/evidence['before']['elapsed_seconds']),2),'frame_30_time_reduction_percent':round(100*(1-evidence['after']['frame_times_seconds']['30']/evidence['before']['frame_times_seconds']['30']),2),'fixed_loop_speedup':hotloop['speedup'],'fixed_loop_evidence':str(ROOT/'logs/performance-hotloop.json'),'fixed_loop_state_identical':True,'diagnostic_scope':'Same resource directory, 200 Present cutoff, profiling and periodic backbuffer capture. Real-time animations, audio and capture counts can vary; this is an end-to-end diagnostic, not a fixed instruction workload.','whole_game_verified':False}
    latest=json.loads((ROOT/'logs/performance-after.json').read_text())
    if latest.get('activation_attempts'):
        record['after']['activation_diagnostic']=True
        record['comparison_caveat']='The current run also exercises mouse activation. Elapsed-time ratios describe these diagnostic runs; they do not measure startup speed versus the original game.'
    (ROOT/'logs/performance-comparison.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))

if __name__=='__main__':main()
