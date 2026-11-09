"""Derive bounded music-unlock persistence from existing cached AOT C++."""
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def digest(data):return hashlib.sha256(data).hexdigest()

def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)

def prepare():
    folder=ROOT/'generated/shared-aot';output=folder/'native-progress';inputs={};outputs={}
    sites={'functions_7.cpp':{
        '40535c':'if constexpr(!Metered)if(source_music_flags_write(s,m)){s.r[wr::EAX]=4096;goto L_405361;}',
        '405361':'if constexpr(!Metered)source_music_flags_complete(s,m);'},
        'functions_0.cpp':{'44c36a':'if constexpr(!Metered)source_music_flags_closed(s,m);'}}
    for name,changes in sites.items():
        source=(folder/name).read_bytes();inputs[(folder/name).relative_to(ROOT).as_posix()]=digest(source)
        text=source.decode('utf-8');newline='\r\n' if '\r\n' in text else '\n'
        text='#include "music_flag_save.hpp"'+newline+text
        for a,b in [('#include "bodies.hpp"','#include "../bodies.hpp"'),('#include "../../','#include "../../../')]:text=text.replace(a,b)
        for label,addition in changes.items():
            anchor=f'L_{label}: {{{newline}s.eip=0x{label}u;{newline}if constexpr(Metered)wr::tick(budget,s.eip);{newline}'
            if text.count(anchor)!=1:raise ValueError('Ambiguous cached music-save site '+label)
            text=text.replace(anchor,anchor+addition+newline)
        data=text.encode('utf-8');path=output/name;write(path,data);outputs[path.relative_to(ROOT).as_posix()]=digest(data)
    inputs[Path(__file__).relative_to(ROOT).as_posix()]=digest(Path(__file__).read_bytes())
    metadata=dict(schema='wa2.derived-progress.v1',inputs=inputs,outputs=outputs,semantic_sites=3,cached_inputs_unchanged=True,source_executable_opened=False)
    write(output/'provenance.json',(json.dumps(metadata,indent=2)+'\n').encode());return metadata
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
