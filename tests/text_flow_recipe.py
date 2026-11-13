"""Finite transformation guards using cached C++/constants, never an EXE."""
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'scripts'))
spec=importlib.util.spec_from_file_location('text_flow',root/'scripts/prepare_shared_text_flow.py')
recipe=importlib.util.module_from_spec(spec);spec.loader.exec_module(recipe)
source=(root/'generated/shared-aot/native-language/functions_hot.cpp').read_text(encoding='utf-8')
code=bytes(map(int,re.findall(rb'\d+',(root/'generated/static-data/text.inc').read_bytes())));base=0x401000
result,targets=recipe.derive(source,code,base)
assert len(targets)==22 and result==recipe.derive(source,code,base)[0]
assert result.count('if constexpr(!Metered)goto L_text_local_dispatch;')==3
assert result.count('local_text_jumps.take()')==22
assert result.count('default:local_text_jumps.external();')==1
original=(root/'generated/shared-aot/functions_5.cpp').read_text(encoding='utf-8')
glyphs=(root/'generated/shared-aot/native-text/functions_5.cpp').read_text(encoding='utf-8')
hook='if constexpr(!Metered)if(wa2::aot::native_draw_glyph(s,m))return;'
assert original.count(hook)==1
original=original.replace(hook,hook.replace('native_draw_glyph(','native_draw_glyph_or_yield('))
start=original.index('template<bool Metered> void f_4462e0(')
end=original.index('template void f_4462e0<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);',start)+len('template void f_4462e0<false>(Context&,wr::Memory&,Scope&,std::uint64_t&,std::uint32_t);')
rest=original[:start]+original[end:]
anchor='L_474f00: {\ns.eip=0x474f00u;\nif constexpr(Metered)wr::tick(budget,s.eip);\n'
assert rest.count(anchor)==1
rest='#include "text_ftol.hpp"\n'+rest.replace(anchor,anchor+'if constexpr(!Metered)if(native_integer_ftol(s,m,scope))return;\n')
assert glyphs==rest.replace('#include "bodies.hpp"','#include "../bodies.hpp"').replace('#include "../../','#include "../../../')
body,_=recipe.scoped_stack(original[start:end])
assert body in (root/'generated/shared-aot/native-text/text_helpers.cpp').read_text(encoding='utf-8')
rejections=0
def rejects(text,data):
    global rejections
    try:recipe.derive(text,data,base)
    except (ValueError,IndexError):rejections+=1
    else:raise AssertionError('Unproved source shape accepted')
rejects(source,code[:0x447700-base]);bad=bytearray(code);struct.pack_into('<I',bad,0x44788c-base,0xdead1234);rejects(source,bad)
for site in recipe.SITES:
    marker=f'L_{site:x}: {{\ns.eip=0x{site:x}u;'
    assert source.count(marker)==1
    rejects(source.replace(marker,marker+'\ninteger.template push<32>(0x12345678u);'),code)
old='if constexpr(!Metered)(void)floating.external([&]{return integer.external([&]{return call_source_indirect(s,m,scope);});});'
begin=source.index('template<bool Metered,bool NativeRegisters> void f_446500(')
rejects(source[:begin]+source[begin:].replace(old,'return;',1),code)
text,count=recipe.scoped_stack('wr::push(s,m,value);wr::push(s,m,0x123u,32);auto a=wr::pop(s,m);auto b=wr::pop(s,m,32);')
assert count==4 and text=='text_push(s,scope,value);text_push(s,scope,0x123u);auto a=text_pop(s,scope);auto b=text_pop(s,scope);'
for bad in ('wr::push(s,m,value,16);wr::pop(s,m);','wr::push(s,m,value);wr::pop(s,m,16);','wr::push(s,m,side_effect());wr::pop(s,m);'):
    try:recipe.scoped_stack(bad)
    except ValueError:rejections+=1
    else:raise AssertionError('Changed stack shape accepted')
report=dict(schema='wa2.text-flow-recipe-check.v1',local_jmp_sites=3,local_targets=len(targets),rejections=rejections,deterministic=True,source_executable_opened=False,passed=True)
if len(sys.argv)>1:Path(sys.argv[1]).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
