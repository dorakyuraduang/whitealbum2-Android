"""Build gate: generated assembly and recovery format must match the generator."""
from pathlib import Path
import hashlib
import json
ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def check():
    expected=digest(ROOT/'scripts/generate_native_backend_v2.py')
    for name in ('native_backend','native_linear'):
        manifest=json.loads((ROOT/'port'/(name+'_manifest.json')).read_text())
        if manifest.get('generator_sha256')!=expected:raise ValueError('Native generator/manifest mismatch: '+name)
        if manifest.get('assembly_sha256')!=digest(ROOT/'port'/(name+'.S')):raise ValueError('Native assembly changed after generation: '+name)
        replacements=json.loads((ROOT/'port/native_functions.json').read_text())
        if manifest.get('native_function_manifest_sha256')!=digest(ROOT/'port/native_functions.json') or manifest.get('native_source_functions')!=replacements['functions']:
            raise ValueError('Native source function manifest changed')
        if manifest.get('retired_module_manifest_sha256')!=digest(ROOT/'port/retired_modules.json'):
            raise ValueError('Retired module boundaries changed')
        if manifest['instructions']+manifest['source_replaced_instructions']+manifest['retired_module_instructions']!=138806:
            raise ValueError('Native instruction coverage changed outside the native resource functions')
        if not manifest.get('checked_cpp_fallback_retained'):raise ValueError('Missing compatibility fallback')
        if name=='native_linear' and manifest.get('static_memory_fault_table_entry_bytes')!=6:raise ValueError('Native recovery table format mismatch')
    print('Native generation hashes, instruction coverage and recovery format verified.')

if __name__=='__main__':check()
