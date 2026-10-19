"""Check real DirectSound refills, including ordinary-launch binary identity."""
from pathlib import Path
import argparse
import hashlib
import json
import statistics

ROOT = Path(__file__).resolve().parent.parent


def bound(path):
    return {'file': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def stream(case):
    buffers = {}
    sources = []
    for path in sorted(case.glob('runtime*.json')):
        report = json.loads(path.read_text(encoding='utf-8-sig'))
        state = report.get('process', report)
        sources.append(bound(path))
        for item in state.get('directsound', {}).get('buffer_timing', []):
            target = buffers.setdefault(item.get('lifetime',item['object']), {'info': item, 'events': {}})
            target['info'] = item
            for event in item['recent']:
                key = tuple(event[k] for k in ['milliseconds', 'operation', 'thread', 'offset', 'bytes', 'sample_hash'])
                target['events'][key] = event
    candidates = []
    for item in buffers.values():
        info = item['info']
        events = sorted(item['events'].values(), key=lambda event: event['milliseconds'])
        plays = [event for event in events if event['operation'] == 'play']
        if not plays or not info['bytes_per_second']:
            continue
        start = plays[0]['milliseconds']
        stops = [event['milliseconds'] for event in events if event['operation'] == 'stop' and event['milliseconds'] >= start]
        end = min(stops, default=float('inf'))
        writes = [event for event in events if event['operation'] == 'unlock' and event['thread'] > 1
                  and info['bytes'] / 4 <= event['bytes'] <= info['bytes'] / 2
                  and start <= event['milliseconds'] < end]
        if len(writes) >= 2:
            candidates.append((info, writes))
    if not candidates:
        raise ValueError('No streaming worker PCM buffer was observed')
    info, writes = max(candidates, key=lambda item: len(item[1]))
    durations = [1000 * event['bytes'] / info['bytes_per_second'] for event in writes]
    late = [event for event, duration in zip(writes, durations) if event['locked_ms'] >= duration]
    overwritten = [event for event in writes if (event['play_cursor'] - event['offset']) % info['bytes'] < event['bytes']]
    gaps = [b['milliseconds'] - a['milliseconds'] for a, b in zip(writes, writes[1:])]
    repeated_half = sum(a['offset'] == b['offset'] for a, b in zip(writes, writes[1:]))
    result = {'buffer_bytes': info['bytes'], 'bytes_per_second': info['bytes_per_second'],
              'buffer_seconds': info['bytes'] / info['bytes_per_second'],
              'refills': len(writes), 'observed_stream_seconds': (writes[-1]['milliseconds'] - start) / 1000,
              'pcm_seconds_per_refill': statistics.median(durations) / 1000,
              'refill_ms': {'min': min(e['locked_ms'] for e in writes),
                            'median': statistics.median(e['locked_ms'] for e in writes),
                            'max': max(e['locked_ms'] for e in writes)},
              'max_refill_interval_ms': max(gaps), 'refills_over_decode_budget': len(late),
              'refills_reaching_playing_half': len(overwritten), 'consecutive_same_half_writes': repeated_half,
              'pcm_sample_hashes': [event['sample_hash'] for event in writes], 'runtime_sources': sources}
    result['refill_deadlines_met'] = not late and not overwritten and not repeated_half and max(gaps) < 1300
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline-case', type=Path, required=True)
    parser.add_argument('--baseline-program', type=Path, required=True)
    parser.add_argument('--launch-report', type=Path, action='append', required=True)
    args = parser.parse_args()
    output = ROOT / 'out/WA2_SDL2_x64.exe'
    expected = bound(output)['sha256']
    baseline = stream(args.baseline_case)
    baseline['program'] = bound(args.baseline_program)
    fixed = []
    for source in args.launch_report:
        launch = json.loads(source.read_text())
        if launch['output_sha256'] != expected:
            raise ValueError('Launch used a different executable')
        if launch['launcher_return_code'] != 0 or launch['forced_diagnostic_cleanup'] or not launch['exit_confirmed']:
            raise ValueError('Launch did not exit normally')
        if any(flag in launch['command'] for flag in ['--profile', '--frames', '--snapshot']):
            raise ValueError('Audio validation must use ordinary runtime options')
        result = stream(Path(launch['case']))
        count = min(len(baseline['pcm_sample_hashes']), len(result['pcm_sample_hashes']))
        result['baseline_pcm_sample_prefix_matches'] = count >= 10 and baseline['pcm_sample_hashes'][:count] == result['pcm_sample_hashes'][:count]
        result['launch_report'] = bound(source)
        result['launch_mode'] = launch['launch_mode']
        result['launch_method'] = launch['launch_method']
        result['native_console'] = launch['native_console']
        if not result['refill_deadlines_met'] or result['observed_stream_seconds'] < 45 or not result['baseline_pcm_sample_prefix_matches']:
            raise ValueError('BGM stream validation failed: ' + json.dumps(result, ensure_ascii=False))
        fixed.append(result)
    proof = {'schema': 'wa2.bgm-stream-verification.v1', 'output_sha256': expected,
             'baseline': baseline, 'fixed': fixed,
             'scope': 'Real-game title BGM PCM continuity and native buffer refill deadlines; whole-game frame rate is separate.',
             'auditory_listening_performed': False}
    destination = ROOT / 'logs/bgm-stream-verification.json'
    destination.write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({'proof': str(destination), 'baseline_refill_ms': baseline['refill_ms'],
                      'fixed_refill_ms': [item['refill_ms'] for item in fixed],
                      'refill_deadlines_met': True, 'pcm_sample_prefix_matches': True}, indent=2))


if __name__ == '__main__':
    main()
