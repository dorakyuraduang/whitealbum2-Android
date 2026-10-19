"""Shared release-code retirement boundaries; the complete oracle is test-only."""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'port/retired_modules.json'
DOCUMENT=json.loads(MANIFEST.read_text())
FUNCTIONS=json.loads((ROOT/'port/native_functions.json').read_text())
if DOCUMENT['input_sha256']!=FUNCTIONS['input_sha256']:raise ValueError('Retirement image mismatch')

def address(value):return int(value,0) if isinstance(value,str) else value

RANGES=tuple((address(m['start']),address(m['end'])) for m in DOCUMENT['modules'])
GATEWAY_NAMES={name for m in DOCUMENT['modules'] for name in m['native_gateways']}
GATEWAYS=tuple(f for f in FUNCTIONS['functions'] if f['name'] in GATEWAY_NAMES)
if {f['name'] for f in GATEWAYS}!=GATEWAY_NAMES:raise ValueError('Missing native retirement gateway')
RELEASE_EXCLUSIONS=RANGES+tuple((f['start'],f['end']) for f in GATEWAYS)

def contains(pc,ranges=RANGES):return any(start<=pc<end for start,end in ranges)
