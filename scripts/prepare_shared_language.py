"""Derive language-aware source expressions from immutable cached AOT C++."""
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def prepare():
    folder=ROOT/'generated/shared-aot';output=folder/'native-language';inputs={};outputs={}
    for name,label in [('functions_4.cpp','4454c4'),('functions_hot.cpp','446835')]:
        source=(folder/name).read_bytes();inputs[(folder/name).relative_to(ROOT).as_posix()]=digest(source)
        text=source.decode('utf-8');newline='\r\n' if '\r\n' in text else '\n'
        text='#include "language_text.hpp"'+newline+text
        for a,b in [('#include "bodies.hpp"','#include "../bodies.hpp"'),('#include "../../','#include "../../../')]:text=text.replace(a,b)
        anchor=f'L_{label}: {{{newline}s.eip=0x{label}u;{newline}'
        if text.count(anchor)!=1:raise ValueError('Ambiguous cached quote site')
        start=text.index(anchor);end=text.index(newline+'}',start);body=text[start:end]
        if body.count('auto value=0x27u;')!=1:raise ValueError('Changed cached quote semantics')
        text=text[:start]+body.replace('auto value=0x27u;','auto value=source_quote_byte(m);')+text[end:]
        if name=='functions_hot.cpp':
            anchor=f'L_447205: {{{newline}s.eip=0x447205u;{newline}'
            if text.count(anchor)!=1:raise ValueError('Ambiguous cached advance site')
            start=text.index(anchor);end=text.index(newline+'}',start);body=text[start:end]
            old='auto value=integer.template get<0,32,0>();'
            if body.count(old)!=1:raise ValueError('Changed cached advance semantics')
            replacement='auto value=source_text_advance(s,m,integer.template get<4,32,0>(),integer.template get<5,32,0>(),integer.template get<0,32,0>());integer.template set<0,32,0>(value);'
            text=text[:start]+body.replace(old,replacement)+text[end:]
        data=text.encode('utf-8');path=output/name;write(path,data);outputs[path.relative_to(ROOT).as_posix()]=digest(data)
    inputs[Path(__file__).relative_to(ROOT).as_posix()]=digest(Path(__file__).read_bytes())
    metadata=dict(schema='wa2.derived-language.v1',inputs=inputs,outputs=outputs,semantic_sites=3,cached_inputs_unchanged=True,source_executable_opened=False)
    write(output/'provenance.json',(json.dumps(metadata,indent=2)+'\n').encode());return metadata
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
