#include "native_sprites.hpp"
#include "native_glyphs.hpp"
#include "host_calls.hpp"
#include "extended80.hpp"
#include "../engine/math_environment.hpp"
#include "../engine/glyph_index.hpp"
#include "../engine/glyph_geometry_memo.hpp"
#include "../engine/single_tile_geometry.hpp"
#include <winrecomp/runtime.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#pragma STDC FENV_ACCESS ON
namespace wa2::aot {namespace {
thread_local NativeSpriteCounts counts;
thread_local NativeGlyphCounts glyph_counts;
thread_local std::vector<SpriteTile> tile_buffer;
thread_local wa2::text::GlyphIndex glyph_index;
thread_local wa2::text::GlyphGeometryMemo<SpriteTile> glyph_geometry;
struct Declined {};
template<class T> T read(const std::uint8_t* bytes,unsigned at){T value;std::memcpy(&value,bytes+at,sizeof(value));return value;}
float ordinary(float value){
    // Every arithmetic boundary rejects nonfinite/subnormal binary32 values.
    // Classify their representation once, avoiding floating comparisons and
    // general libm classification on each sprite/grid operation. Both signed
    // zeros remain accepted. Rejection stays inside the saved host environment.
    const auto bits=std::bit_cast<std::uint32_t>(value),exponent=bits&0x7f800000u;
    if(exponent==0x7f800000u || (!exponent && (bits&0x007fffffu)))throw Declined{};
    return value;
}
// Keep the PC24 source's explicit single-precision operation boundaries.
float add(float a,float b){return ordinary(a+b);}float sub(float a,float b){return ordinary(a-b);}
float mul(float a,float b){return ordinary(a*b);}float div(float a,float b){if(!b)throw Declined{};return ordinary(a/b);}
float mix(float a,float b,float t){return add(mul(sub(b,a),t),a);}
float clamp(float x){return x>=1?1.f:x<=0?0.f:x;}
std::pair<int,int> grid_axis(int count,int size,float from,float to,float extent,
        std::array<float,18>& lower,unsigned lower_offset,std::array<float,18>& upper,unsigned upper_offset,std::array<float,18>& position){
    int first=0,last=count;
    // Integral UVs cover glyph cells and ordinary image rectangles. Every
    // omitted prefix cell has lower==upper==1, hence an exact zero cumulative
    // span. Its divisions are normal and nonzero. Suffix cells are never used
    // by an emitted vertex. Endpoints and dimensions are bounded to 32767, so
    // none of the omitted arithmetic can raise a rejected FP exception.
    // Fractional/subpixel rectangles retain the complete checked arithmetic.
    if(count>1 && from==float(int(from)) && to==float(int(to))){
        first=std::min(count,int(from)/size);
        last=std::min(count,(int(to)+size-1)/size);
    }
    float accumulated=0;
    for(int n=first;n<=last;++n){
        lower[n+lower_offset]=clamp(div(sub(from,float(n*size)),float(size)));
        upper[n+upper_offset]=clamp(div(sub(to,float(n*size)),float(size)));
        position[n]=div(mul(accumulated,float(size)),extent);
        accumulated=add(sub(upper[n+upper_offset],lower[n+lower_offset]),accumulated);
    }
    return {first,last};
}
int channel(unsigned sum,unsigned palette,bool additive){return additive?int(sum)+int(palette)*4-512+(palette>128?2:0):palette<=128?int(sum*palette/128):int(sum+(palette-128)*(510-sum)/127);}
bool integer_glyph_geometry(SpriteCommand& command,const std::array<std::uint8_t,256>& s,
    bool half_image,int width,int height,int tw,int th,int nx,int ny,std::span<const std::uint32_t> handles,
    std::uint32_t half_bits,std::uint32_t colour,std::uint32_t secondary){
    using wa2::geometry::fixed_coordinate;using wa2::geometry::dyadic_bits;
    if((tw&(tw-1)) || (th&(th-1)))return false;
    std::array<std::int32_t,4> uv;
    for(unsigned n=0;n<4;++n){const auto value=fixed_coordinate(read<std::uint32_t>(s.data(),0xbc+n*4));if(!value || *value<0)return false;
        if(half_image && (*value&1))return false;uv[n]=half_image?*value/2:*value;}
    if(uv[1]<=uv[0] || uv[3]<=uv[2] || uv[1]>width*256 || uv[3]>height*256)return false;
    const auto x=unsigned(uv[0])/(unsigned(tw)*256),y=unsigned(uv[2])/(unsigned(th)*256);
    if(x>=unsigned(nx) || y>=unsigned(ny) || uv[1]>int((x+1)*tw*256) || uv[3]>int((y+1)*th*256))return false;
    const auto half=fixed_coordinate(half_bits);if(!half)return false;
    const auto normal_bits=[](std::uint32_t bits){const auto magnitude=bits&0x7fffffffu;return magnitude<0x7f800000u && (!magnitude || magnitude>=0x00800000u);};
    const auto z=read<std::uint32_t>(s.data(),0xa8);if(!normal_bits(z))return false;
    std::array<std::int32_t,4> px,py;std::array<std::uint32_t,4> rhw;
    for(unsigned n=0;n<4;++n){const auto sx=fixed_coordinate(read<std::uint32_t>(s.data(),0x88+n*4)),sy=fixed_coordinate(read<std::uint32_t>(s.data(),0x98+n*4));
        if(!sx || !sy || *sx < -4194304 || *sx>4194304 || *sy < -4194304 || *sy>4194304)return false;
        px[n]=*sx-*half;py[n]=*sy-*half;
        if(px[n] < -8388608 || px[n]>8388608 || py[n] < -8388608 || py[n]>8388608)return false;
        rhw[n]=read<std::uint32_t>(s.data(),0xac+n*4);if(!normal_bits(rhw[n]))return false;
    }
    const auto u_bits=8u+std::countr_zero(unsigned(tw)),v_bits=8u+std::countr_zero(unsigned(th));
    const auto left=dyadic_bits(uv[0]-int(x*tw*256),u_bits),right=dyadic_bits(uv[1]-int(x*tw*256),u_bits),
               top=dyadic_bits(uv[2]-int(y*th*256),v_bits),bottom=dyadic_bits(uv[3]-int(y*th*256),v_bits);
    tile_buffer.clear();
    if(const auto texture=handles[y*unsigned(nx)+x]){
        auto& tile=tile_buffer.emplace_back();tile.texture=texture;
        for(unsigned n=0;n<4;++n)tile.vertices[n]={std::bit_cast<float>(dyadic_bits(px[n],8)),std::bit_cast<float>(dyadic_bits(py[n],8)),
            std::bit_cast<float>(z),std::bit_cast<float>(rhw[n]),colour,secondary,
            std::bit_cast<float>(n&1?right:left),std::bit_cast<float>(n>>1?bottom:top)};
    }
    command.tiles=tile_buffer;return true;
}
bool build(wr::Cpu& cpu,wr::Memory& memory,SpriteCommand& command,const std::array<std::uint8_t,256>& s,std::uint32_t override_image,bool memo_glyph=false,bool integer_glyph=false){
    if(!memory.load(0xb34004,32))return false;
    const auto si=[&](unsigned at){return read<std::int16_t>(s.data(),at);};
    const auto su=[&](unsigned at){return read<std::uint32_t>(s.data(),at);};
    const auto sf=[&](unsigned at){return ordinary(read<float>(s.data(),at));};
    if(si(0x4a)<0 || si(0x4c)>=0 || s[0x15]!=4 || s[0x16] || su(0xdc) || su(0x44)>3)return false;
    if(!override_image && (si(0x4a)<0 || si(0x4a)>=1024))return false;
    const auto resource=override_image?override_image:0xa93fb8+unsigned(si(0x4a))*128;
    std::array<std::uint8_t,128> image;memory.copy_out(resource,image);
    const auto dim=[&](unsigned at){return read<std::int16_t>(image.data(),at);};
    const int width=dim(0x5c),height=dim(0x5e),tw=dim(0x60),th=dim(0x62),nx=dim(0x64),ny=dim(0x66);
    if(!image[0] || width<=0 || height<=0 || tw<=0 || th<=0 || nx<1 || nx>16 || ny<1 || ny>16)return false;
    const auto palette_index=std::bit_cast<std::int8_t>(s[0x52]);
    std::array<std::uint8_t,4> palette;memory.copy_out(std::uint32_t(0x4c2290+int(palette_index)*4),palette);
    for(unsigned n=1;n<4;++n)if(su(0x53+n*4)!=su(0x53) || su(0x63+n*4)!=su(0x63))return false;
    std::uint32_t colour=su(0x63),secondary=0xff000000u|(su(0x53)&0xffffffu);
    if(palette[3]<=1){colour&=0xff000000u;secondary=0xff000000u;
        for(unsigned n=0;n<3;++n){const auto c=unsigned(std::clamp(channel(unsigned(s[0x53+n])+s[0x63+n],palette[2-n],palette[3]!=0),0,510));
            colour|=std::min(c,255u)<<(n*8);secondary|=(c-std::min(c,255u))<<(n*8);}}
    const auto filter=su(0x40),linear=memory.load(0xb3442c,32);
    command.minimum=command.magnification=(linear?filter==1:filter-1u<2)?2:1;
    command.mipmap=command.minimum;
    if(linear && filter==2){const auto caps=memory.load(0xb342c0,32);command.mipmap=2;
        command.minimum=caps&0x10000000?7:caps&0x08000000?6:caps&0x04000000?3:2;
        command.magnification=caps&0x1000?7:caps&0x800?6:caps&0x400?3:2;
    }
    command.device=memory.load(0xb34438,32);
    if(si(0x48)<0)command.target=memory.load(memory.load(0xb34428,32)?0xb3443c:0xb34000,32);
    else{
        if(si(0x48)>=1024)return false;
        const auto target=0xa93fb8+unsigned(si(0x48))*128;
        // State 2 creates/clears a texture. Keep that first-use side effect
        // in the checked function; already-created targets have no such work.
        if(memory.load(target+0x68,16)!=1)return false;
        const auto sp=std::uint64_t(cpu.r[wr::ESP]);
        if(sp+0x128>0xb34008ull && sp<0xb3600cull)return false;
        memory.check(0xb34008,4,wr::Memory::Write);
        if(memory.permissions(0xb34008)&wr::Memory::Execute)return false;
        command.target=memory.load(memory.load(target+0x70,32),32);
        if(!command.target)return false;
        command.target_kind=SpriteTarget::texture_level_zero;
        if(std::uint64_t(resource)<0xb3400cull && std::uint64_t(resource)+128>0xb34008ull)return false;
    }
    command.blend_enabled=1;command.blend_op=1;
    const auto blend=su(0x3c);const bool separate=bool(memory.load(0xb342a0,32)&0x800);
    switch(blend){
    case 1:case 11:command.source_blend=5;command.destination_blend=6;break;
    case 2:command.source_blend=2;command.destination_blend=6;break;
    case 3:command.source_blend=5;command.destination_blend=2;break;
    case 4:command.source_blend=5;command.destination_blend=4;break;
    case 5:command.source_blend=9;command.destination_blend=3;break;
    case 6:command.source_blend=1;command.destination_blend=3;break;
    case 7:command.blend_op=separate?2:1;command.source_blend=separate?5:1;command.destination_blend=separate?2:3;break;
    case 8:command.source_blend=1;command.destination_blend=4;break;
    case 9:command.blend_op=separate?3:1;command.source_blend=separate?5:1;command.destination_blend=separate?2:4;break;
    case 10:command.source_blend=10;command.destination_blend=4;break;
    default:command.blend_enabled=0;command.source_blend=2;command.destination_blend=1;
    }
    command.alpha_argument=dim(0x68)==1?1:2; // selects which ALPHAARG slot the source changes below
    const auto half_bits=memory.load(0x4c2280,32),pointer=read<std::uint32_t>(image.data(),0x70);
    std::array<std::uint32_t,256> handles{};
    if(command.target_kind==SpriteTarget::texture_level_zero && std::uint64_t(pointer)<0xb3400cull && std::uint64_t(pointer)+nx*ny*4>0xb34008ull)return false;
    memory.copy_out(pointer,{reinterpret_cast<std::uint8_t*>(handles.data()),std::size_t(nx*ny)*4});
    const bool memo=memo_glyph && !su(0x74);
    const wa2::text::GlyphGeometryMemo<SpriteTile>::Input key{s,image,palette,half_bits,std::span(handles).first(nx*ny)};
    if(integer_glyph){
        return integer_glyph_geometry(command,s,image[4]!=0,width,height,tw,th,nx,ny,key.handles,half_bits,colour,secondary);
    }
    if(memo)if(const auto tiles=glyph_geometry.find(key)){command.tiles=*tiles;return true;}
    float u0=sf(0xbc),u1=sf(0xc0),v0=sf(0xc4),v1=sf(0xc8);
    std::array<std::array<float,2>,4> corners;
    const auto ox=sf(0x78),oy=sf(0x7c),sx=sf(0x80),sy=sf(0x84);
    float cosine=1,sine=0;
    if(su(0x74)){
        if(!memory.load(0xbc830c,32) || (cpu.fp.get32(24)&0xffc0u)!=0x1f80u)return false;
        std::uint64_t radians;memory.copy_out(0x4a22f0,{reinterpret_cast<std::uint8_t*>(&radians),8});
        fp::Environment e{cpu.fp.get16(0),0};
        const auto phase_word=fp::to_binary(fp::multiply(fp::from_binary(su(0x74),32,e),fp::from_binary(radians,64,e),e),32,e);
        const auto phase=ordinary(std::bit_cast<float>(std::uint32_t(phase_word)));
        if(phase==0)sine=phase;
        else{
            if(!wa2::math::exact_trigonometry_input(phase))return false;
            const auto expected=wa2::math::trigonometry_source_words();auto scope=memory.execution_scope();
            for(const auto at:{0x4a84d0u,0x4ac710u}){const auto bytes=scope.readable(at,expected.size_bytes());
                if(bytes.size()!=expected.size_bytes() || std::memcmp(bytes.data(),expected.data(),bytes.size()))return false;}
            cosine=ordinary(float(wa2::math::cosine_finite(phase).value));sine=ordinary(float(wa2::math::sine_finite(phase).value));
        }
    }
    if(u0<0 || v0<0)return false;
    for(unsigned n=0;n<4;++n){float dx=sub(sf(0x88+n*4),ox),dy=sub(sf(0x98+n*4),oy);
        if(su(0x44)&1)dx=-dx;if(su(0x44)&2)dy=-dy;
        corners[n]={sub(add(mul(add(mul(sine,dy),mul(cosine,dx)),sx),ox),mul(sx,0)),sub(add(mul(sub(mul(cosine,dy),mul(sine,dx)),sy),oy),mul(sy,0))};
    }
    if(image[4]){u0=mul(u0,.5f);u1=mul(u1,.5f);v0=mul(v0,.5f);v1=mul(v1,.5f);}
    if(!(u1>u0 && v1>v0) || u1>width || v1>height)return false;
    const auto du=sub(u1,u0),dv=sub(v1,v0),half=ordinary(std::bit_cast<float>(half_bits));
    if(const auto single=wa2::geometry::single_tile_geometry(corners,{u0,u1,v0,v1},tw,th,nx,ny)){
        tile_buffer.clear();
        if(handles[single->index]){
            auto& tile=tile_buffer.emplace_back();tile.texture=handles[single->index];
            for(unsigned n=0;n<4;++n){const auto px=filter==3?sf(0x88+n*4):corners[n][0],py=filter==3?sf(0x98+n*4):corners[n][1];
                tile.vertices[n]={sub(px,half),sub(py,half),sf(0xa8),sf(0xac+n*4),colour,secondary,single->uv[n][0],single->uv[n][1]};}
        }
        command.tiles=tile_buffer;
        if(memo && !std::fetestexcept(FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW))glyph_geometry.remember(key,command.tiles);
        return true;
    }
    std::array<float,18> left{},right{},top{},bottom{},tx{},ty{};
    const auto [first_x,last_x]=grid_axis(nx,tw,u0,u1,du,left,0,right,1,tx);
    const auto [first_y,last_y]=grid_axis(ny,th,v0,v1,dv,top,1,bottom,0,ty);
    tile_buffer.clear();if(tile_buffer.capacity()<256)tile_buffer.reserve(256);
    for(int y=first_y;y<last_y;++y)for(int x=first_x;x<last_x;++x){
        if(top[y+1]==bottom[y] || right[x+1]==left[x] || !handles[y*nx+x] || !(u0<float((x+1)*tw) && v0<float((y+1)*th) && float(x*tw)<u1 && float(y*th)<v1))continue;
        auto& tile=tile_buffer.emplace_back();tile.texture=handles[y*nx+x];
        for(unsigned n=0;n<4;++n){const auto col=x+int(n&1),row=y+int(n>>1);auto& out=tile.vertices[n];
            const float px=filter==3?sf(0x88+n*4):mix(mix(corners[0][0],corners[2][0],ty[row]),mix(corners[1][0],corners[3][0],ty[row]),tx[col]);
            const float py=filter==3?sf(0x98+n*4):mix(mix(corners[0][1],corners[2][1],ty[row]),mix(corners[1][1],corners[3][1],ty[row]),tx[col]);
            out={sub(px,half),sub(py,half),sf(0xa8),sf(0xac+n*4),colour,secondary,n&1?right[x+1]:left[x],n>>1?bottom[y]:top[y+1]};
        }
    }
    command.tiles=tile_buffer;if(memo && !std::fetestexcept(FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW))glyph_geometry.remember(key,command.tiles);return true;
}
}
NativeSpriteCounts native_sprite_counts(){return counts;}
bool native_draw_sprite(wr::Cpu& cpu,wr::Memory& memory){
    auto* service=SpriteService::find(memory);if(!service || cpu.eip!=0x43b7a0)return false;
    if(cpu.fp.bytes[4] || (cpu.fp.get16(0)&0xf3f)!=0x3f){++counts.declined;++counts.fp_declined;return false;}
    if(current_native_calls && !current_native_calls->remaining)return false;
    SpriteCommand command{};std::uint32_t returning{};bool capability_filter{};
    try{auto scope=memory.execution_scope();returning=memory.load(cpu.r[wr::ESP],32);
        wa2::math::detail::NativeEnvironment environment;
        const auto sp=cpu.r[wr::ESP];if(std::uint64_t(sp)+12>0x100000000ull)return false;
        const auto sprite=memory.load(sp+4,32);if(!sprite)return false;
        std::array<std::uint8_t,256> snapshot;memory.copy_out(sprite,snapshot);
        if(read<std::int16_t>(snapshot.data(),0x48)>=0 && std::uint64_t(sprite)<0xb3400cull && std::uint64_t(sprite)+256>0xb34008ull)return false;
        capability_filter=read<std::uint32_t>(snapshot.data(),0x40)==2 && memory.load(0xb3442c,32);
        if(!build(cpu,memory,command,snapshot,memory.load(sp+8,32)) || std::fetestexcept(FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW)){++counts.declined;return false;}
    }catch(const Declined&){++counts.declined;return false;}catch(const wr::GuestFault&){++counts.declined;return false;}
    if(!service->submit(command)){++counts.declined;++counts.adapter_declined;return false;}
    if(current_native_calls)--current_native_calls->remaining;
    ++counts.accepted;counts.tiles+=command.tiles.size();counts.offscreen+=command.target_kind==SpriteTarget::texture_level_zero;counts.capability_filter+=capability_filter;
    cpu.r[wr::EAX]=0;cpu.r[wr::ESP]+=4;cpu.eip=returning;return true;
}
NativeGlyphCounts native_glyph_counts(){return glyph_counts;}
bool prepare_integer_glyph(wr::Cpu& cpu,wr::Memory& memory,SpriteCommand& command,std::array<std::uint8_t,256> sprite,const std::array<std::uint32_t,9>& args){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    if(wa2::geometry::single_tile_reference_enabled)return false;
#endif
    // No fenv transaction: all calculations below are exact integers and the
    // output IEEE words are constructed directly. Preserve allocator errno.
    struct SavedError {int value=errno;~SavedError(){errno=value;}} saved_error;
    using wa2::geometry::fixed_coordinate;using wa2::geometry::dyadic_bits;
    const auto word=[&](unsigned at){return read<std::uint32_t>(sprite.data(),at);};
    if(word(0x74) || word(0x44) || word(0x80)!=0x3f800000u || word(0x84)!=0x3f800000u)return false;
    const auto ox=fixed_coordinate(word(0x78)),oy=fixed_coordinate(word(0x7c)),x=fixed_coordinate(word(0x88)),y=fixed_coordinate(word(0x98));
    if(!ox || !oy || !x || !y || *ox < -4194304 || *ox>4194304 || *oy < -4194304 || *oy>4194304)return false;
    const auto i=[&](unsigned n){return std::bit_cast<std::int32_t>(args[n]);};
    std::array<std::int32_t,4> positions;
    for(unsigned n=0;n<4;++n){const auto value=i(n+1);if(value < -16384 || value>16384)return false;
        positions[n]=value*256+(n&1?*y:*x);if(positions[n] < -4194304 || positions[n]>4194304)return false;}
    if(i(5)<0 || i(5)>32768 || i(6)<=0 || i(6)>32768)return false;
    auto code=std::uint16_t(args[0]);if(std::uint16_t(code+0x4f)>0x2c && code>255)code=std::uint16_t((code<<8)|(code>>8));
    auto scope=memory.execution_scope();const auto table=scope.readable(0x4a14b8,256);if(table.size()!=256)return false;
    const auto resolved=glyph_index.lookup(std::span<const std::uint8_t,256>{table.data(),256},code);if(!resolved || *resolved>0x7fffffffu)return false;
    const std::int64_t cell=i(8)<2?40:24,u=std::int64_t(*resolved%80)*cell+i(5),v=std::int64_t(*resolved/80)*cell+i(5);
    if(u<0 || v<0 || u+i(6)>32768 || v+i(6)>32768)return false;
    const auto put=[&]<class T>(unsigned at,T value){std::memcpy(sprite.data()+at,&value,sizeof(value));};
    const auto style=read<std::int16_t>(sprite.data(),0x50);
    const auto source_cell=std::int32_t(std::bit_cast<std::int8_t>(std::uint8_t(memory.load(std::uint32_t(0xa823b3+int(style)*0x34),8))));
    if(source_cell!=std::bit_cast<std::int32_t>(args[3]-args[1]))put(0x40,1u);
    put(0x4a,std::uint16_t(read<std::uint16_t>(sprite.data(),0x4a)+args[8]));
    for(unsigned n=0;n<4;++n){put(0x88+n*4,dyadic_bits(positions[n&1?2:0],8));put(0x98+n*4,dyadic_bits(positions[n>>1?3:1],8));
        put(0x53+n*4,0xff000000u);put(0x63+n*4,args[7]);}
    put(0xbc,dyadic_bits(std::int32_t(u*256),8));put(0xc0,dyadic_bits(std::int32_t((u+i(6))*256),8));
    put(0xc4,dyadic_bits(std::int32_t(v*256),8));put(0xc8,dyadic_bits(std::int32_t((v+i(6))*256),8));put(0x3c,1u);
    if(!build(cpu,memory,command,sprite,0,true,true))return false;
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    ++wa2::geometry::single_tile_diagnostics.integer_glyphs;
#endif
    return true;
}
bool native_draw_glyph(wr::Cpu& cpu,wr::Memory& memory){
    auto* service=SpriteService::find(memory);if(!service || cpu.eip!=0x4462e0 || cpu.fp.bytes[4] || (cpu.fp.get16(0)&0xf3f)!=0x3f)return false;
    if(current_native_calls && !current_native_calls->remaining)return false;
    SpriteCommand command{};std::uint32_t returning{};
    try{auto scope=memory.execution_scope();const auto sp=cpu.r[wr::ESP];if(std::uint64_t(sp)+0x128>0x100000000ull)return false;
        returning=memory.load(sp,32);std::array<std::uint8_t,256> sprite;memory.copy_out(sp+4,sprite);
        std::array<std::uint32_t,9> args;memory.copy_out(sp+0x104,{reinterpret_cast<std::uint8_t*>(args.data()),sizeof(args)});
        if(!prepare_integer_glyph(cpu,memory,command,sprite,args)){
        wa2::math::detail::NativeEnvironment environment;
        const auto put=[&]<class T>(unsigned at,T value){std::memcpy(sprite.data()+at,&value,sizeof(value));};
        const auto i=[&](unsigned n){return std::bit_cast<std::int32_t>(args[n]);};
        const auto f=[&](unsigned n){return ordinary(float(i(n)));};
        const auto style=read<std::int16_t>(sprite.data(),0x50);
        const auto cell=std::int32_t(std::bit_cast<std::int8_t>(std::uint8_t(memory.load(std::uint32_t(0xa823b3+int(style)*0x34),8))));
        if(cell!=std::bit_cast<std::int32_t>(args[3]-args[1]))put(0x40,1u);
        put(0x4a,std::uint16_t(read<std::uint16_t>(sprite.data(),0x4a)+args[8]));
        const auto x=ordinary(read<float>(sprite.data(),0x88)),y=ordinary(read<float>(sprite.data(),0x98));
        const auto left=add(f(1),x),right=add(x,f(3)),top=add(y,f(2)),bottom=add(y,f(4));
        for(unsigned n=0;n<4;++n){put(0x88+n*4,n&1?right:left);put(0x98+n*4,n>>1?bottom:top);put(0x53+n*4,0xff000000u);put(0x63+n*4,args[7]);}
        auto code=std::uint16_t(args[0]);if(std::uint16_t(code+0x4f)>0x2c && code>255)code=std::uint16_t((code<<8)|(code>>8));
        const auto table=scope.readable(0x4a14b8,256);if(table.size()!=256)return false;
        const auto resolved=glyph_index.lookup(std::span<const std::uint8_t,256>{table.data(),256},code);
        if(!resolved || *resolved>0x7fffffffu)return false;const auto index=*resolved;
        const float cell_size=i(8)<2?40.f:24.f;
        const auto u=add(f(5),mul(float(index%80),cell_size)),v=add(mul(float(index/80),cell_size),f(5));
        put(0xbc,u);put(0xc0,add(f(6),u));put(0xc4,v);put(0xc8,add(v,f(6)));put(0x3c,1u);
        if(!build(cpu,memory,command,sprite,0,true) || std::fetestexcept(FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW)){++glyph_counts.declined;return false;}
        }
    }catch(const Declined&){++glyph_counts.declined;return false;}catch(const wr::GuestFault&){++glyph_counts.declined;return false;}
    if(!service->submit(command)){++glyph_counts.declined;return false;}
    if(current_native_calls)--current_native_calls->remaining;
    ++glyph_counts.accepted;glyph_counts.tiles+=command.tiles.size();cpu.r[wr::EAX]=0;cpu.r[wr::ESP]+=4;cpu.eip=returning;return true;
}
}
