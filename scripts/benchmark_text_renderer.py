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
    parser.add_argument('--output',type=Path,default=ROOT/'logs/pc-text-renderer-comparison.json')
    options=parser.parse_args()
    if options.glyph_only and options.control_word!='0x7f':parser.error('--glyph-only uses the admitted PC24 float geometry')
    low_priority();r=ROOT;b=r/'build/shared-aot-refresh-x64';d=r/'build/pc-text-renderer';d.mkdir(exist_ok=True)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS
    t=(r/'tests/novel_cpu.cpp').read_text();t=t[:t.index('    const auto initial_cpu=p.cpu;')]
    t=t.replace('#include "../','#include "'+r.as_posix()+'/')
    header=r"""
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
    t=t.replace('CHECK(argc==2);','CHECK(argc==5);')
    t=t.replace('a::Runtime runtime;','auto host=std::make_shared<HiddenSurface>();a::Runtime runtime({}, {},host,false);')
    setup=r"""
     runtime.load();const auto setup=p.allocate_bytes(4096);
     const auto api=[&](const char* lib,const char* name,std::initializer_list<U32> values){return p.callback(p.resolve(lib,name),std::span(values.begin(),values.size()));};
     const auto com=[&](U32 object,unsigned slot,std::initializer_list<U32> values){std::vector<U32> args{object};args.insert(args.end(),values.begin(),values.end());return p.callback(p.memory.load(p.memory.load(object,32)+slot*4,32),args);};
     const std::array<U32,14> pp{1280,720,21,1,0,0,1,1,1,0,0,0,0,0};p.memory.copy_in(setup,{reinterpret_cast<const std::uint8_t*>(pp.data()),sizeof(pp)});
     const auto d3d=api("d3d9.dll","Direct3DCreate9",{32});CHECK(d3d);CHECK(!com(d3d,16,{0,1,1,0x22,setup,setup+128}));
     const auto real_device=p.memory.load(setup+128,32);CHECK(real_device);CHECK(!com(real_device,18,{0,0,0,setup+128}));const auto back=p.memory.load(setup+128,32);
     std::vector<U32> font_textures;
     for(unsigned n=0;n<16;++n){CHECK(!api("d3dx9_41.dll","D3DXCreateTexture",{real_device,256,256,1,0,21,1,setup+128}));font_textures.push_back(p.memory.load(setup+128,32));}
     const auto data=
    """
    t=t.replace('runtime.load();const auto data=',setup)
    t=t.replace('p.memory.store(0xb34438,device,32);','p.memory.store(0xb34438,real_device,32);').replace('p.memory.store(0xb34000,data+0x2000,32);p.memory.store(0xb3443c,data+0x2000,32);','p.memory.store(0xb34000,back,32);p.memory.store(0xb3443c,back,32);')
    t=t.replace('p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32)','p.memory.store(data+0x4000+n*4,font_textures[n%font_textures.size()],32)')
    scale_init='p.memory.store(output+0x80,0x3f800000,32);p.memory.store(output+0x84,0x3f800000,32);'
    if t.count(scale_init)!=1:raise ValueError('Changed fixture font-scale setup')
    t=t.replace(scale_init,'p.memory.store(output+0x80,U32(std::stoul(argv[3],nullptr,0)),32);p.memory.store(output+0x84,U32(std::stoul(argv[3],nullptr,0)),32);')
    t+=r"""
     std::array<std::uint8_t,0x2000> initial;p.memory.copy_out(output,initial);const auto saved=p.cpu;
     using Finish=void(*)();const auto finish=reinterpret_cast<Finish>(SDL_GL_GetProcAddress("glFinish"));CHECK(finish);
     std::ofstream report(argv[1]);report<<"{\"actual_scale_bits\":"<<p.memory.load(output+0x80,32)<<",\"cases\":[";bool comma=false;
     const auto begin=a::native_glyph_counts();
     for(unsigned length:{40u,80u,160u,240u,400u}){
      for(unsigned round=0;round<4;++round){
       p.cpu=saved;p.cpu.fp.put16(0,std::uint16_t(std::stoul(argv[2],nullptr,0)));p.memory.copy_in(output,initial);
       const auto before=p.state().reports.at("direct3d9")();
       const auto start=std::chrono::steady_clock::now();CHECK(!com(real_device,41,{}));
       if(std::string_view(argv[4])=="glyph"){
        const auto sp=p.cpu.r[wr::ESP]-0x4000;std::array<std::uint8_t,256> packet;p.memory.copy_out(output,packet);p.memory.copy_in(sp+4,packet);p.memory.store(sp,0xf0001234,32);
        for(unsigned block=0;block<length;block+=256){a::GlyphCallScope glyphs(p.memory);auto scope=p.memory.execution_scope();
         for(unsigned n=block;n<std::min(length,block+256);++n){
          const U32 x=14+n%50*22,y=28+n/50*40;const std::array<U32,9> args{U32('A'+n%26),x,y,x+28,y+28,4,28,0xffffffff,0};
          for(unsigned k=0;k<args.size();++k)scope.store<32>(sp+0x104+k*4,args[k]);p.cpu.eip=0x4462e0;p.cpu.r[wr::ESP]=sp;CHECK(a::native_draw_glyph(p.cpu,p.memory));
         }
        }
       }else for(unsigned part=0;part<length;part+=200){const auto count=std::min(200u,length-part);std::string text;
        for(unsigned n=0;n<count;++n){if(n && n%50==0)text+='\n';text+=char('A'+n%26);}const auto raw=p.put_string(text);
        std::array<U32,26> args{output,0,14,28+part/200*200,4,0,14,0,0,0,28,raw,0xffffffffu,0,255,255,255,255,0,0,0,0,0,0,data+0x3000,1};
        p.callback(0x446500,args,false);
       }
       CHECK(!com(real_device,42,{}));CHECK(!com(real_device,43,{0,0,1,0,0x3f800000,0}));
       const auto submitted=std::chrono::steady_clock::now();finish();const auto done=std::chrono::steady_clock::now();
       const auto after=p.state().reports.at("direct3d9")();
       if(comma)report<<',';comma=true;report<<"{\"characters\":"<<length<<",\"round\":"<<round<<",\"submit_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(submitted-start).count()<<",\"complete_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(done-start).count()<<",\"before\":"<<before<<",\"after\":"<<after<<'}';
      }
     }
     const auto end=a::native_glyph_counts();report<<"],\"native_accepted\":"<<end.accepted-begin.accepted<<",\"native_declined\":"<<end.declined-begin.declined<<",\"screen_read\":false,\"game_launched\":false,\"hidden_component_window\":true,\"passed\":true}\n";CHECK(report);std::cout<<"Finite hidden PC text renderer check finished.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
    """
    source=d/'fixture.cpp';source.write_text(t,encoding='utf-8');obj=d/'fixture.obj'
    command=next(x['command'] for x in json.loads((b/'compile_commands.json').read_text()) if x['file'].replace('\\','/').endswith('/tests/novel_cpu.cpp'))
    if '"' in command:raise ValueError('Unquoted Windows workspace paths are required')
    args=command.split();args[args.index('-o')+1]=str(obj);args[-1]=str(source)
    block=(b/'build.ninja').read_text().split('build wa2_refresh_novel_cpu.exe:',1)[1].split('\n  LINK_LIBRARIES = ',1)[1].splitlines()[0];libraries=shlex.split(block)
    env=os.environ.copy();env.update(ZIG_GLOBAL_CACHE_DIR=str(r/'build/test-content-046/zig-cache'),ZIG_LOCAL_CACHE_DIR=str(r/'build/zig-local'))
    old=None
    if options.baseline_dir:
        folder=options.baseline_dir.resolve();old=folder/'libwa2_shared_aot.a';proof=json.loads((folder/'before-verification.json').read_text())
        if not proof['passed'] or proof['windows_library']['sha256']!=digest(old):raise ValueError('A verified retained archive is required')
    binaries=[];pairs=[]
    with (d/'check.log').open('wb') as log:
        def run(argv):subprocess.run(list(map(str,argv)),cwd=b,creationflags=flags,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180,check=True)
        run(args)
        for side in range(2 if old else 1):
            inputs=libraries.copy()
            if old and not side:inputs[inputs.index('shared_aot/libwa2_shared_aot.a')]=str(old)
            binary=d/('fixture-'+str(side)+'.exe');binaries.append(binary)
            run([r/'tools/python/ziglang/zig.exe','c++','-target','x86_64-windows-gnu','-Os',obj,'-o',binary,*inputs])
        for pair in range(3):
            observations=[None]*len(binaries)
            for order in range(len(binaries)):
                side=(pair+order)%len(binaries);path=d/('pair-'+str(pair)+'-'+str(side)+'.json')
                run([binaries[side],path,options.control_word,options.scale_bits,'glyph' if options.glyph_only else 'text']);observations[side]=json.loads(path.read_text())
                if not observations[side]['passed'] or observations[side]['actual_scale_bits']!=int(options.scale_bits,16):raise ValueError('Finite renderer fixture/scale setup failed')
            if old:
                for a,before in zip(observations[1]['cases'],observations[0]['cases']):
                    for key in ('draws','submitted_draws','compact_screen_unique_vertices'):
                        if a['after'][key]-a['before'][key]!=before['after'][key]-before['before'][key]:raise ValueError('Changed text submission topology')
            pairs.append(observations)
    result=[]
    for length in (40,80,160,240,400):
        times=[[c['submit_ns'] for pair in pairs for c in pair[side]['cases'] if c['characters']==length and c['round']] for side in range(len(binaries))]
        row=dict(characters=length,median_ns=[statistics.median(x) for x in times])
        if old:row['speedup']=row['median_ns'][0]/row['median_ns'][1]
        result.append(row)
    report=dict(schema='wa2.pc-text-renderer-check.v1',control_word=options.control_word,scale_bits=options.scale_bits,glyph_only=options.glyph_only,results=result,same_object_sha256=digest(obj),fixture_sha256=digest(source),script_sha256=digest(Path(__file__)),old_library_sha256=digest(old) if old else None,new_library_sha256=digest(r/'build/shared-aot-refresh-x64/shared_aot/libwa2_shared_aot.a'),driver=pairs[0][-1]['cases'][0]['after']['driver'],hidden_graphics_component=True,game_launched=False,screen_access=False,phone_fps_measured=False,passed=True)
    options.output.parent.mkdir(parents=True,exist_ok=True);options.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
