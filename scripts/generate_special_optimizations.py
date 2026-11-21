"""Bind shared optimizations to the checked SC cache, without reading an EXE."""
from __future__ import annotations
import re
import struct
from pathlib import Path


def number(value):
    return int(value, 0) if isinstance(value, str) else value


class Optimizations:
    def __init__(self, root, contracts, graph, bodies, edges, merged, owners, write):
        from generate_shared_aot import portable_floating
        self.profile = contracts['source_optimizations']
        self.text = {k: number(v) for k, v in self.profile['text'].items()
                     if k not in ('jump_tables', 'layout_slots')}
        self.scalar = {number(self.profile[k]['entry']) for k in ('ftol', 'sine', 'cosine')}
        self.scalar.add(number(self.profile['atan2']))
        self.music = {k: number(v) for k, v in self.profile['music'].items()}
        self.hook_owners = {owners[pc] for pc in self.scalar | {
            self.music['write_call'], self.music['write_return'], self.music['closed'], self.text['entry']}}
        self.tables = {}
        image = (root/'generated/special-contents/image.bin').read_bytes()
        for site, address, count in self.profile['text']['jump_tables']:
            site, address = number(site), number(address)
            section, = [s for s in graph['image']['sections'] if
                graph['image']['image_base']+s['rva'] <= address < graph['image']['image_base']+s['rva']+s['raw_size']]
            offset = address-graph['image']['image_base']-section['rva']+section['raw_offset']
            values = struct.unpack_from('<'+'I'*count, image, offset)
            if any(owners.get(pc) != self.text['entry'] for pc in values):
                raise ValueError('SC text table leaves its checked static function')
            if 'jump_indirect' not in {e['kind'] for e in edges[site]}:
                raise ValueError('SC text table is not the checked indirect JMP')
            self.tables[site] = sorted(set(values))
        # Only ordinary two-byte glyphs, without wrap, markup, ruby, output
        # writes or foreign calls. Every departure cancels before executing.
        self.region = ((0x448080,0x44818c),(0x4483bd,0x4483f4),
                       (0x448b12,0x448b4d),(0x448d52,0x448f67),
                       (0x449086,0x4490a4),(0x44929c,0x4492c6))
        self.allowed = {pc for pc in merged[self.text['entry']]['locations']
                        if any(first <= pc < end for first,end in self.region)}
        # This finite union has only these three source callees. Writes stay
        # within EBP's fixed frame, the typed stack packet, and the source
        # speed remainder (inside its separately bound helper).
        allowed_calls = {self.text['speed'],self.text['glyph_entry'],number(self.profile['ftol']['entry'])}
        for pc in self.allowed:
            for edge in edges[pc]:
                if edge['kind']=='call_direct' and edge['to'] not in allowed_calls:
                    raise ValueError('New call in SC ordinary text replay region')
                if edge['kind'] in ('call_indirect','call_import','jump_import','return','stop'):
                    raise ValueError('Foreign boundary in SC ordinary text replay region')
        # Readonly source constants are protected by the executable epoch;
        # capture all absolute data reads as well, including writable flags.
        rows = {r['address']:r for r in graph['instructions']}
        globals_ = set()
        for pc in self.allowed:
            row=rows[pc]
            if row['mnemonic']=='lea':continue
            for op in row.get('operands',[]):
                if op['kind']=='memory' and op['base']=='none' and op['index']=='none':
                    globals_.add((op['displacement'] & 0xffffffff,(op['width']+7)//8))
        if sum(n for _,n in globals_)>1024:raise ValueError('SC text global key exceeds its fixed capacity')
        lines=['#pragma once','#include "text_ftol.hpp"','#include "native_math.hpp"',
               '#include "music_flag_save.hpp"','#include "profile_write_source.hpp"',
               '#include "source_text_replay.hpp"','#include "render_profile.hpp"','namespace wa2::special {']
        for key,ctype,name,fields in (
            ('ftol','aot::IntegerFtolSource','integer_ftol_source',('entry','feature','store_ip')),
            ('sine','aot::TrigonometrySource','sine_source',('entry','feature','table')),
            ('cosine','aot::TrigonometrySource','cosine_source',('entry','feature','table')),
            ('music','aot::MusicFlagSource','music_flag_source',('flags','path','registration_return','write_return','registration_slot','close_frame','close_return_slot')),
            ('profile_write','aot::ProfileWriteSource','profile_write_source',('api_return','caller_first','caller_last','owner','update','caller_slot','section_slot','key_slot'))):
            lines.append('inline constexpr '+ctype+' '+name+'{'+','.join(str(number(self.profile[key][f]))+'u' for f in fields)+'};')
        lines += [f'inline constexpr std::uint32_t atan2_entry={number(self.profile["atan2"])}u;',
                  'inline constexpr std::array<std::pair<wr::U32,unsigned>,'+str(len(globals_))+'> text_globals{{'+','.join('{'+str(at)+'u,'+str(n)+'u}' for at,n in sorted(globals_))+'}};']
        fields=('entry','layout_begin','outline_end','fill_end','page_begin','glyph_call','glyph_return','glyph_entry','remainder')
        values=[self.text[k] for k in fields]+[number(self.profile['ftol']['feature']),self.text['font_metrics']]
        values += [self.text[k] for k in ('frame_bytes','frame_capture','argument_bytes')]
        values += [self.profile['text']['layout_slots'][0]]+[self.text[k] for k in ('string_slot','cursor_slot','page_output_slot','page_ruby_slot')]
        lines += ['inline constexpr aot::SourceTextProfile text_profile{'+','.join(map(str,values))+',text_globals,drawing_source};','}']
        write('optimization_profile.hpp','\n'.join(lines)+'\n')
        # Instruction-level oracle used only by finite memory tests. All input
        # is the existing checked cache, including the FTOL merged CRT owner.
        pcs={pc for owner in self.hook_owners | {owners[number(self.profile['atan2'])]} for pc in merged[owner]['locations']}
        write('optimization_oracle.inc','\n'.join(f'case 0x{pc:x}u:{{'+portable_floating(bodies[pc][2])+'return true;}' for pc in sorted(pcs))+'\n')
        self.stack_sites=0
        callers=sorted({edge['to'] for pc in merged[number(self.profile['profile_write']['owner'])]['locations']
            if any(e['kind']=='call_direct' and e['to']==0x44f1a0 for e in edges[pc])
            for edge in edges[pc] if edge['kind']=='call_continuation'})
        if len(callers)!=53:raise ValueError('SC exit profile save sequence changed')
        write('profile_write_callers.hpp','#pragma once\n#include <cstdint>\nnamespace wa2::special {inline constexpr std::uint32_t profile_write_callers[]={'+','.join(str(pc)+'u' for pc in callers)+'};}\n')

    def transform(self, entry, pc, native):
        if entry==self.text['entry'] and pc in self.tables and pc not in (0x448695,0x448a6c):
            old='if constexpr(!Metered)(void)floating.external([&]{return integer.external([&]{return call_source_indirect(s,m,scope);});});'
            if native.count(old)!=1:raise ValueError('SC local text JMP lost its original checked dispatcher')
            native=native.replace(old,'if constexpr(!Metered)goto L_text_local_dispatch;')
        return native

    def prelude(self,entry):
        if entry!=self.text['entry']:return []
        return ['aot::TextJumpBudget<Metered> local_text_jumps;',
                'aot::SourceTextReplay text_replay(m,text_profile,!Metered && NativeRegisters && !optimization_reference);']

    def hooks(self,entry,pc,hot):
        def external(code):
            return 'floating.external([&]{return integer.external([&]{'+code+'});})' if hot else '([&]{'+code+'})()'
        result=[]
        if pc in self.scalar:
            result += ['if constexpr(!Metered)if('+external('return scalar_optimization(s,m,scope);')+')return;']
        if pc==self.music['write_call']:
            result += ['if constexpr(!Metered)if(!optimization_reference && aot::source_music_flags_write(s,m)){s.r[wr::EAX]=4096;goto L_'+format(self.music['write_return'],'x')+';}']
        if pc==self.music['write_return']:
            result += ['if constexpr(!Metered)if(!optimization_reference)aot::source_music_flags_complete(s,m);']
        if pc==self.music['closed']:
            result += ['if constexpr(!Metered)if(!optimization_reference)aot::source_music_flags_closed(s,m);']
        if entry!=self.text['entry']:return result
        if pc not in self.allowed:
            result += ['if constexpr(!Metered)text_replay.abandon();']
        if pc==self.text['layout_begin']:
            result += ['if constexpr(!Metered && NativeRegisters){const auto next='+external('return text_replay.layout_begin(s,scope);')+';switch(next){'+
                       ''.join(f'case 0x{self.text[k]:x}:goto L_{self.text[k]:x};' for k in ('outline_end','fill_end'))+'}}']
        if pc in (self.text['outline_end'],self.text['fill_end']):
            result += ['if constexpr(!Metered && NativeRegisters)(void)'+external(f'text_replay.layout_finish(s,scope,0x{pc:x}u);return true;')+';']
        if pc==self.text['page_begin']:
            result += ['if constexpr(!Metered && NativeRegisters){const auto next='+external('return text_replay.page_begin(s,scope);')+';switch(next){'+
                       ''.join(f'case 0x{self.text[k]:x}:goto L_{self.text[k]:x};' for k in ('page_begin','glyph_call'))+'}}']
        if pc==self.text['glyph_call']:
            result += ['if constexpr(!Metered && NativeRegisters)(void)'+external('text_replay.page_finish(s,scope);return true;')+';']
        return result

    def end(self,entry):
        if entry!=self.text['entry']:return []
        targets=sorted({pc for values in self.tables.values() for pc in values})
        return ['L_text_local_dispatch:', 'if constexpr(!Metered){switch(s.eip){',
                *[f'case 0x{pc:x}u:if(local_text_jumps.take())goto L_{pc:x};return;' for pc in targets],
                'default:text_replay.abandon();local_text_jumps.external();',
                '(void)floating.external([&]{return integer.external([&]{return call_source_indirect(s,m,scope);});});return;}}']
