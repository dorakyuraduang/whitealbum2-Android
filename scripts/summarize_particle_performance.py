"""Summarize bounded component evidence without inventing scene/device FPS."""
import hashlib
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[1]


def load(path):
    return json.loads((ROOT/path).read_text(encoding='utf-8'))


def paired(rows):
    return dict(samples=rows,median_paired_speedup=statistics.median(a/b for a,b in rows))


def main():
    proof=load('logs/shared-aot-verification.json')
    if not proof.get('passed'):raise ValueError('Common-source checks did not pass')
    for name,expected in proof['source_sha256'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=expected:
            raise ValueError('Stale verification: '+name)
    comparison=load('logs/particle-cpu-comparison-0.2.18.json')
    if not comparison.get('passed'):raise ValueError('Missing bounded paired CPU comparison')
    crt=proof['crt_math_paths'];heap=proof['shared_heap_pool_memory']
    report=dict(schema='wa2.particle-component-evidence.v1',release=load('config/release.json')['name'],
        one_game_source_set=True,game_launched=False,screen_access=False,device_fps_measured=False,
        overall_tenfold_speedup_established=False,
        verification_sha256=hashlib.sha256((ROOT/'logs/shared-aot-verification.json').read_bytes()).hexdigest(),
        cpu_function_comparisons=comparison['comparisons'],
        near_axis_trigonometry=dict(**paired(crt['near_axis_static_native_ns']),calls_per_sample=crt['near_axis_leaf_calls_per_sample'],
            exact_circle_phase_cases=crt['circle_phase_bit_exact_cases'],maximum_binary64_ulp_error=crt['maximum_trig_candidate_ulps']),
        repeated_angle_cache=dict(**paired(crt['repeated_angle_cache_ns']),sine_cosine_pairs_per_sample=crt['repeated_angle_pairs_per_sample']),
        temporary_heap_pool=heap,
        common_quad_stream=dict(before_bytes=960,after_vertex_bytes=144,after_index_bytes=12,after_bytes=156,
            byte_reduction_fraction=1.-156/960.,gpu_timing_measured=False),
        limits=[
            'The component improvements overlap; they cannot be multiplied into a scene speedup.',
            'The synthetic sprite CPU fixtures disable native CRT replacements for exact full-state comparison.',
            'Measurements include fixture overhead and scheduler noise on this Windows computer.',
            'The compact stream is verified in memory; no actual GPU, frame loop, Android device or whole-game timing was run.',
            'Admitted native paths retain checked static bodies for unsupported inputs; overall migration remains incomplete.'])
    (ROOT/'logs/performance-particles-0.2.18.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS bounded component summary; no overall FPS or tenfold claim')


if __name__=='__main__':main()
