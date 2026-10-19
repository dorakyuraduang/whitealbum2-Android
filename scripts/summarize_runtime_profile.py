"""Compact diagnostics from a sustained-launch case; never drives the game."""
from pathlib import Path
import argparse
import json


def load(path):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    return data.get('process', data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('case', type=Path)
    parser.add_argument('--start', type=int, default=30)
    parser.add_argument('--end', type=int, default=45)
    args = parser.parse_args()
    before = load(args.case / f'runtime-menu-plus-{args.start}.json')
    after = load(args.case / f'runtime-menu-plus-{args.end}.json')
    seconds = args.end - args.start
    frames = after['direct3d9']['presents'] - before['direct3d9']['presents']
    wall_before = {row['page']: row['ms'] for row in before.get('wall_profile', {}).get('guest_page_ms', [])}
    pages = sorted((dict(page=hex(row['page']), ms=round(row['ms']-wall_before.get(row['page'], 0), 2),
                         hot_pcs=[hex(pc['pc']) for pc in row['hot_pcs'][:5]])
                    for row in after.get('wall_profile', {}).get('guest_page_ms', [])),
                   key=lambda row: row['ms'], reverse=True)
    entries = after.get('static_batches', {}).get('profile', [])
    result = dict(case=str(args.case), nominal_interval_seconds=seconds, frames=frames, fps=frames/seconds,
                  guest_instructions_per_frame=(after['native_instructions']-before['native_instructions'])/max(frames, 1),
                  draws_per_frame=(after['direct3d9']['draw_calls']-before['direct3d9']['draw_calls'])/max(frames, 1),
                  pages=pages[:15],
                  fast_paths=[dict(entry=hex(row['entry']), attempts=row['attempts'], batches=row['batches'],
                                   success_ratio=row['batches']/max(1, row['attempts']),
                                   last_control=hex(row['last_fpu_control'])) for row in entries])
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
