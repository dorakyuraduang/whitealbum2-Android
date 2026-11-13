"""Finite PC text submission check through the actual shared renderer.

Explicitly authorized hidden graphics component check only. No game entry,
original EXE, PAK, input operation, screenshot or screen read. Reports are
component CPU/GPU completion times, never phone or whole-game FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
from build_pak_tool import low_priority
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-hidden-graphics',action='store_true',required=True)
    parser.add_argument('--baseline-dir',type=Path)
    parser.add_argument('--control-word',choices=('0x7f','0x27f','0x37f'),default='0x27f')
    parser.add_argument('--scale-bits',choices=('0x3f800000','0x3f700000'),default='0x3f800000')
    parser.add_argument('--glyph-only',action='store_true',help='Isolate typed glyph geometry/cache submissions; use PC24')
    parser.add_argument('--text-kind',choices=('ascii','cjk'),default='ascii')
    parser.add_argument('--font-textures',type=int,choices=(16,195),default=16)
    parser.add_argument('--compare-renderer',action='store_true',help='Also link the verified retained presentation archive')
    parser.add_argument('--verify-commands',action='store_true',help='Compare owned submitted CPU vertices/indices/state; never reads pixels')
    parser.add_argument('--state-barriers',action='store_true',help='Interleave alpha, viewport and generic-draw transitions')
    parser.add_argument('--output',type=Path,default=ROOT/'logs/pc-text-renderer-comparison.json')
    options=parser.parse_args()
    if options.glyph_only and options.control_word!='0x7f':parser.error('--glyph-only uses the admitted PC24 float geometry')
    low_priority();r=ROOT;b=r/'build/shared-aot-refresh-x64';d=r/'build/pc-text-renderer';d.mkdir(exist_ok=True)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    t=(r/'tests/novel_cpu.cpp').read_text();t=t[:t.index('    const auto initial_cpu=p.cpu;')]
    t=t.replace('#include "../','#include "'+r.as_posix()+'/')
    header='#define SDL_MAIN_HANDLED\n#include <mojoshader_assembler.h>\n#include "'+(r/'tests/owned_gl_trace.hpp').as_posix()+'"\n'+r"""
    #define SDL_MAIN_HANDLED
    #include <SDL.h>
    #include "D:/wa2/platform/render/surface_host.hpp"
    struct HiddenSurface final:wa2::graphics::SurfaceHost {
     SDL_Window* window{};
     HiddenSurface(){if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
     SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);}
     ~HiddenSurface(){if(window)SDL_DestroyWindow(window);SDL_QuitSubSystem(SDL_INIT_VIDEO);}
     SDL_Window* acquire(std::uint32_t,std::uint32_t w,std::uint32_t h,bool)override{
      if(!window)window=SDL_CreateWindow("WA2 finite text check",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,int(w),int(h),SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);return window;}
     bool embedded_gl()const noexcept override{return false;}
     void source_cursor(std::int32_t,std::int32_t)override{}
     int cursor_visible(bool)override{return 0;}
     std::array<int,2> client_size(std::uint32_t)const override{return {1280,720};}
     void frame_submitted()override{}
    };
    """
    header=header.replace('D:/wa2',r.as_posix());where=t.index('int main(');t=t[:where]+header+t[where:]
    t=t.replace('CHECK(argc==2);','CHECK(argc==9);owned_gl_trace::enabled=std::string_view(argv[7])=="trace";')
    t=t.replace('a::Runtime runtime;','auto host=std::make_shared<HiddenSurface>();a::Runtime runtime({}, {},host,false);')
    setup=r"""
     runtime.load();const auto setup=p.allocate_bytes(4096);
     const auto api=[&](const char* lib,const char* name,std::initializer_list<U32> values){return p.callback(p.resolve(lib,name),std::span(values.begin(),values.size()));};
     const auto com=[&](U32 object,unsigned slot,std::initializer_list<U32> values){std::vector<U32> args{object};args.insert(args.end(),values.begin(),values.end());return p.callback(p.memory.load(p.memory.load(object,32)+slot*4,32),args);};
     const std::array<U32,14> pp{1280,720,21,1,0,0,1,1,1,0,0,0,0,0};p.memory.copy_in(setup,{reinterpret_cast<const std::uint8_t*>(pp.data()),sizeof(pp)});
     const auto d3d=api("d3d9.dll","Direct3DCreate9",{32});CHECK(d3d);CHECK(!com(d3d,16,{0,1,1,0x22,setup,setup+128}));
     const auto real_device=p.memory.load(setup+128,32);CHECK(real_device);CHECK(!com(real_device,18,{0,0,0,setup+128}));const auto back=p.memory.load(setup+128,32);
     std::vector<U32> font_textures;
     for(unsigned n=0;n<unsigned(std::stoul(argv[5]));++n){CHECK(!api("d3dx9_41.dll","D3DXCreateTexture",{real_device,256,256,1,0,21,1,setup+128}));font_textures.push_back(p.memory.load(setup+128,32));}
     U32 barrier_shader{};
     if(std::string_view(argv[8])=="barriers"){
      const std::string code="vs_3_0\ndcl_position0 v0\ndcl_color0 v1\ndcl_position0 o0\ndcl_color0 o1\nmov o0, v0\nmov o1, v1\n";
      const auto* blob=MOJOSHADER_assemble("owned-barrier",code.data(),unsigned(code.size()),nullptr,0,nullptr,0,nullptr,0,nullptr,nullptr,nullptr,nullptr,nullptr);
      CHECK(blob && !blob->error_count);const auto input=p.allocate_bytes(blob->output_len);p.memory.copy_in(input,{reinterpret_cast<const std::uint8_t*>(blob->output),std::size_t(blob->output_len)});
      CHECK(!com(real_device,91,{input,setup+128}));barrier_shader=p.memory.load(setup+128,32);CHECK(barrier_shader);MOJOSHADER_freeParseData(blob);
     }
     const auto data=
    """
    t=t.replace('runtime.load();const auto data=',setup)
    t=t.replace('p.memory.store(0xb34438,device,32);','p.memory.store(0xb34438,real_device,32);').replace('p.memory.store(0xb34000,data+0x2000,32);p.memory.store(0xb3443c,data+0x2000,32);','p.memory.store(0xb34000,back,32);p.memory.store(0xb3443c,back,32);')
    t=t.replace('p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32)','p.memory.store(data+0x4000+n*4,font_textures[n%font_textures.size()],32)')
    t=t.replace('throw std::runtime_error("Constructed text exceeds source text-helper contract");','throw std::runtime_error("Constructed text source diagnostic at "+std::to_string(p.cpu.eip)+": "+p.read_string(p.argument(1),4096));')
    t=t.replace('[](wr::Process&)->U32{throw','[](wr::Process& p)->U32{throw')
    scale_init='p.memory.store(output+0x80,0x3f800000,32);p.memory.store(output+0x84,0x3f800000,32);'
    if t.count(scale_init)!=1:raise ValueError('Changed fixture font-scale setup')
    t=t.replace(scale_init,'p.memory.store(output+0x80,U32(std::stoul(argv[3],nullptr,0)),32);p.memory.store(output+0x84,U32(std::stoul(argv[3],nullptr,0)),32);')
    t='#include "'+(r/'engine/glyph_index.hpp').as_posix()+'"\n'+t
    t+=r"""
     const bool cjk=std::string_view(argv[6])=="cjk";std::vector<U32> cjk_codes;
     if(cjk){std::array<std::uint8_t,256> ranges;p.memory.copy_out(0x4a14b8,ranges);wa2::text::GlyphIndex lookup;
      std::map<unsigned,U32> pages;
      for(unsigned code=0x8140;code<=0xfcfc;++code){const auto lead=code>>8,trail=code&255;
       if(!((lead>=0x81 && lead<=0x9f) || (lead>=0xe0 && lead<=0xfc)) || trail<0x40 || trail>0xfc || trail==0x7f)continue;
       bool member{};for(unsigned row=0;row<64;++row){std::uint16_t lo,hi;std::memcpy(&lo,ranges.data()+row*4,2);std::memcpy(&hi,ranges.data()+row*4+2,2);if(hi==65535 || (row && lo==65535))break;if(lo<=code && code<=hi){member=true;break;}}
       if(!member)continue;const auto index=lookup.lookup(ranges,std::uint16_t(code));if(!index)continue;
       const auto u=*index%80*40+4,v=*index/80*40+4;if(u+28>3200 || v+28>3840 || u%256+28>256 || v%256+28>256)continue;
       const auto page=(v/256*13+u/256)%font_textures.size();pages.try_emplace(page,code);
      }
      for(const auto& [page,code]:pages)cjk_codes.push_back(code);CHECK(cjk_codes.size()>=8);
     }
     std::array<std::uint8_t,0x2000> initial;p.memory.copy_out(output,initial);const auto saved=p.cpu;
     using Finish=void(*)();const auto finish=reinterpret_cast<Finish>(SDL_GL_GetProcAddress("glFinish"));CHECK(finish);
     std::ofstream report(argv[1]);report<<"{\"actual_scale_bits\":"<<p.memory.load(output+0x80,32)<<",\"font_textures\":"<<font_textures.size()<<",\"cjk_pages\":"<<cjk_codes.size()<<",\"cases\":[";bool comma=false;
     const auto begin=a::native_glyph_counts();
     for(unsigned length:{40u,80u,160u,240u,400u,40u,400u}){
      for(unsigned round=0;round<4;++round){
       p.cpu=saved;p.cpu.fp.put16(0,std::uint16_t(std::stoul(argv[2],nullptr,0)));p.memory.copy_in(output,initial);
       const auto before=p.state().reports.at("direct3d9")();
       const auto start=std::chrono::steady_clock::now();CHECK(!com(real_device,41,{}));
       if(std::string_view(argv[4])=="glyph"){
        const auto sp=p.cpu.r[wr::ESP]-0x4000;std::array<std::uint8_t,256> packet;p.memory.copy_out(output,packet);p.memory.copy_in(sp+4,packet);p.memory.store(sp,0xf0001234,32);
        for(unsigned block=0;block<length;block+=256){a::GlyphCallScope glyphs(p.memory);auto scope=p.memory.execution_scope();
         for(unsigned n=block;n<std::min(length,block+256);++n){
          const U32 x=14+n%40*28,y=28+n/40*40,code=cjk?cjk_codes[(n*37)%cjk_codes.size()]:U32('A'+n%26);
          const U32 argument=cjk?((code>>8)|((code&255)<<8)):code;
          const std::array<U32,9> args{argument,x,y,x+28,y+28,4,28,0xffffffff,0};
          for(unsigned k=0;k<args.size();++k)scope.store<32>(sp+0x104+k*4,args[k]);p.cpu.eip=0x4462e0;p.cpu.r[wr::ESP]=sp;CHECK(a::native_draw_glyph(p.cpu,p.memory));
         }
        }
       }else for(unsigned part=0;part<length;part+=40){const auto count=std::min(40u,length-part);std::string text;
        for(unsigned n=0;n<count;++n){if(cjk){const auto code=cjk_codes[((part+n)*37)%cjk_codes.size()];text+=char(code>>8);text+=char(code);}else text+=char('A'+n%26);}const auto raw=p.put_string(text);
        std::array<U32,26> args{output,0,14,28+part/40*40,4,0,14,0,0,0,28,raw,0xffffffffu,0,255,255,255,255,0,0,0,0,0,0,data+0x3000,1};
       p.callback(0x446500,args,false);
       if(std::string_view(argv[8])=="barriers"){
        CHECK(!com(real_device,57,{15,(part/40)%2}));
        CHECK(!com(real_device,65,{0,0}));CHECK(!com(real_device,67,{0,1,2}));CHECK(!com(real_device,67,{0,2,0}));
        CHECK(!com(real_device,67,{0,4,2}));CHECK(!com(real_device,67,{0,5,0}));
        const std::array<std::array<float,4>,4> solid{{{{0,0,0,0}},{{1,0,0,0}},{{0,1,0,0}},{{1,1,0,0}}}};
        auto raw_solid=solid;for(auto& v:raw_solid)v[3]=std::bit_cast<float>(0xff102030u);
        p.memory.copy_in(setup+512,{reinterpret_cast<const std::uint8_t*>(raw_solid.data()),sizeof(raw_solid)});
        CHECK(!com(real_device,89,{0x42}));CHECK(!com(real_device,92,{barrier_shader}));CHECK(!com(real_device,83,{5,2,setup+512,16}));CHECK(!com(real_device,92,{0}));
        const std::array<U32,6> viewport{1,2,1200,680,0,0x3f800000};p.memory.copy_in(setup+640,{reinterpret_cast<const std::uint8_t*>(viewport.data()),sizeof(viewport)});
        CHECK(!com(real_device,47,{setup+640}));
       }
       }
       CHECK(!com(real_device,42,{}));CHECK(!com(real_device,43,{0,0,1,0,0x3f800000,0}));
       const auto submitted=std::chrono::steady_clock::now();finish();const auto done=std::chrono::steady_clock::now();
       const auto after=p.state().reports.at("direct3d9")();
       if(comma)report<<',';comma=true;report<<"{\"characters\":"<<length<<",\"round\":"<<round<<",\"submit_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(submitted-start).count()<<",\"complete_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(done-start).count()<<",\"before\":"<<before<<",\"after\":"<<after<<'}';
      }
     }
     owned_gl_trace::finish(std::string(argv[1])+".commands.bin");
     const auto end=a::native_glyph_counts();report<<"],\"native_accepted\":"<<end.accepted-begin.accepted<<",\"native_declined\":"<<end.declined-begin.declined<<",\"screen_read\":false,\"game_launched\":false,\"hidden_component_window\":true,\"passed\":true}\n";CHECK(report);std::cout<<"Finite hidden PC text renderer check finished.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
    """
    source=d/'fixture.cpp';source.write_text(t,encoding='utf-8');obj=d/'fixture.obj'
    command=next(x['command'] for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].replace('\\','/').endswith('/tests/novel_cpu.cpp'))
    if '"' in command:raise ValueError('Unquoted Windows workspace paths are required')
    args=command.split();args.insert(2,'-DWA2_RENDERER_COMMAND_TRACE='+str(int(options.verify_commands)));args[args.index('-o')+1]=str(obj);args[-1]=str(source)
    block=(b/'build.ninja').read_text().split('build wa2_refresh_novel_cpu.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0];libraries=shlex.split(block)
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(r/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(r/'build/zig-local'))
    old=None;renderer_old=None
    if options.baseline_dir:
        folder=options.baseline_dir.resolve();old=folder/'libwa2_shared_aot.a';proof=json.loads((folder/'before-verification.json').read_text())
        if not proof['passed'] or proof['windows_library']['sha256']!=digest(old):raise ValueError('A verified retained archive is required')
        if options.compare_renderer:
            receipt=json.loads((folder/'renderer-before.json').read_text());renderer_old=folder/'libwa2_shared_graphics.a'
            if not receipt['passed'] or receipt['verification_receipt_sha256']!=digest(folder/'before-verification.json') or receipt['renderer_sha256']!=digest(renderer_old):raise ValueError('The retained renderer must match its passing source-bound receipt')
            if any(proof['source_sha256'].get(n)!=h for n,h in receipt['source_sha256'].items()):raise ValueError('Unverified renderer baseline input')
    if options.compare_renderer and not old:parser.error('--compare-renderer needs --baseline-dir')
    binaries=[];pairs=[];trace_sdl=None
    with (d/'check.log').open('wb') as log:
        def run(argv):subprocess.run(list(map(str,argv)),cwd=b,creationflags=flags,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        if options.verify_commands:
            sdl=next(Path(name) for name in libraries if name.replace('\\','/').endswith('/libSDL2.a'))
            trace_sdl=d/'SDL2-owned-trace.a'
            run([r/'tools/android-sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objcopy.exe','--redefine-sym=SDL_GL_GetProcAddress=__real_SDL_GL_GetProcAddress',sdl,trace_sdl])
        run(args)
        for side in range(2 if old else 1):
            inputs=libraries.copy()
            if old and not side:inputs[inputs.index('shared_aot/libwa2_shared_aot.a')]=str(old)
            if renderer_old and not side:inputs[inputs.index('shared_aot/libwa2_shared_graphics.a')]=str(renderer_old)
            binary=d/('fixture-'+str(side)+'.exe');binaries.append(binary)
            if trace_sdl:inputs[inputs.index(str(sdl).replace('\\','/'))]=str(trace_sdl)
            run([r/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',obj,'-o',binary,*inputs])
        for pair in range(3):
            observations=[None]*len(binaries)
            for order in range(len(binaries)):
                side=(pair+order)%len(binaries);path=d/('pair-'+str(pair)+'-'+str(side)+'.json')
                run([binaries[side],path,options.control_word,options.scale_bits,'glyph' if options.glyph_only else 'text',options.font_textures,options.text_kind,'trace' if options.verify_commands else 'timing','barriers' if options.state_barriers else 'plain']);observations[side]=json.loads(path.read_text())
                if options.verify_commands:observations[side]['command_sha256']=digest(Path(str(path)+'.commands.bin'))
                if not observations[side]['passed'] or observations[side]['actual_scale_bits']!=int(options.scale_bits,16):raise ValueError('Finite renderer fixture/scale setup failed')
            if old:
                if options.verify_commands and observations[0]['command_sha256']!=observations[1]['command_sha256']:raise ValueError('Changed own submitted triangles, texture selection or effective driver state')
                for a,before in zip(observations[1]['cases'],observations[0]['cases']):
                    for key in ('draws','compact_screen_unique_vertices'):
                        if a['after'][key]-a['before'][key]!=before['after'][key]-before['before'][key]:raise ValueError('Changed text submission topology')
            pairs.append(observations)
    result=[]
    for length in (40,80,160,240,400):
        times=[[c['submit_ns'] for pair in pairs for c in pair[side]['cases'] if c['characters']==length and c['round']] for side in range(len(binaries))]
        row=dict(characters=length,median_ns=[statistics.median(x) for x in times])
        row['submitted_draws']=[[c['after']['submitted_draws']-c['before']['submitted_draws'] for pair in pairs for c in pair[side]['cases'] if c['characters']==length and c['round']] for side in range(len(binaries))]
        row['state_applies']=[[c['after']['state_applies']-c['before']['state_applies'] for pair in pairs for c in pair[side]['cases'] if c['characters']==length and c['round']] for side in range(len(binaries))]
        row['stream_allocations']=[[c['after']['stream_storage_allocations']-c['before']['stream_storage_allocations'] if 'stream_storage_allocations' in c['after'] else None for pair in pairs for c in pair[side]['cases'] if c['characters']==length and c['round']] for side in range(len(binaries))]
        if old and not options.verify_commands:row['speedup']=row['median_ns'][0]/row['median_ns'][1]
        result.append(row)
    report=dict(schema='wa2.pc-text-renderer-check.v1',control_word=options.control_word,scale_bits=options.scale_bits,glyph_only=options.glyph_only,text_kind=options.text_kind,font_textures=options.font_textures,cjk_pages=pairs[0][-1]['cjk_pages'],state_barriers=options.state_barriers,results=result,same_object_sha256=digest(obj),fixture_sha256=digest(source),script_sha256=digest(Path(__file__)),trace_header_sha256=digest(r/'tests/owned_gl_trace.hpp'),command_snapshots_verified=options.verify_commands,command_sha256=pairs[0][-1].get('command_sha256'),old_library_sha256=digest(old) if old else None,new_library_sha256=digest(r/'build/shared-aot-refresh-x64/shared_aot/libwa2_shared_aot.a'),old_renderer_sha256=digest(renderer_old) if renderer_old else None,new_renderer_sha256=digest(r/'build/shared-aot-refresh-x64/shared_aot/libwa2_shared_graphics.a'),driver=pairs[0][-1]['cases'][0]['after']['driver'],hidden_graphics_component=True,game_launched=False,screen_access=False,phone_fps_measured=False,passed=True)
    report['trace_instrumented']=options.verify_commands
    report['trace_sdl_sha256']=digest(trace_sdl) if trace_sdl else None
    options.output.parent.mkdir(parents=True,exist_ok=True);options.output.write_text(json.dumps(report,indent=2)+'\n')
    if options.verify_commands:print('PASS exact own submitted vertex/texture/effective-state snapshots in three pairs; trace timings are not performance results.')
    else:print(json.dumps([dict(characters=x['characters'],median_ns=x['median_ns'],speedup=x.get('speedup'),submitted_draws=[sorted(set(s)) for s in x['submitted_draws']],state_applies=[sorted(set(s)) for s in x['state_applies']]) for x in result],indent=2))
if __name__=='__main__':main()
