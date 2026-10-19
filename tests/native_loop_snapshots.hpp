#pragma once
#include "../port/sprite_sort.hpp"
#include "../port/vertex_batch.hpp"
#include "../recompiled/uv_clip.hpp"
#include "../recompiled/native_weather.hpp"
#include "../generated/static-data/static_data.hpp"
#include <cfenv>
#include <cerrno>
extern "C" void wr_x87_216_m0(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m1(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m4(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m6(void*,void*,std::uint32_t*);
extern "C" void wr_x87_217_m3(void*,void*,std::uint32_t*);
namespace wa2::aot {bool native_grid_binary_snapshot(wr::Cpu&,std::uint32_t,unsigned);bool native_grid_stored_binary_snapshot(wr::Cpu&,std::uint32_t,unsigned,std::uint32_t&);}
namespace native_loop_test {
inline unsigned native_arithmetic_accepted{},native_arithmetic_declined{};
inline unsigned native_stored_arithmetic_accepted{},native_stored_arithmetic_declined{};
inline void arithmetic(){
    struct Restore {std::fenv_t state;int error{errno};~Restore(){std::fesetenv(&state);errno=error;}} restore;
    CHECK(!std::feholdexcept(&restore.state));CHECK(!std::fesetenv(FE_DFL_ENV));
    std::mt19937 random(0x43ce95);
    constexpr std::uint32_t edges[]={0,0x80000000,1,0x00800000,0x7f7fffff,0x3f800000,0x3f800001,0x33800000,0x7f800000,0x7fc00000};
    for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD})for(unsigned digit:{0u,1u,4u,6u})for(unsigned n=0;n<384;++n){
        CHECK(!std::fesetround(rounding));CHECK(!std::feclearexcept(FE_ALL_EXCEPT));CHECK(!std::feraiseexcept(FE_DIVBYZERO));errno=EDOM;
        wr::Cpu actual;actual.eip=0x43d3d9;actual.fp.put16(0,0x7f);const auto top=n%8;
        actual.fp.put16(2,std::uint16_t((top<<11)|(random()&0x473f)));actual.fp.bytes[4]=std::uint8_t(1u<<top);
        auto left=(random()&0x807fffff)|((1+random()%254)<<23),right=(random()&0x807fffff)|((1+random()%254)<<23);
        if(n<100){left=edges[n/10];right=edges[n%10];}
        for(unsigned at=0;at<8;++at){wa2::aot::fp::Environment environment;const auto value=wa2::aot::fp::from_binary(at?random():left,32,environment);
            actual.fp.put64(32+at*16,value.significand);actual.fp.put16(40+at*16,value.sign_exponent);}
        const auto original=actual;auto expected=actual;
        const auto host_exceptions=std::fetestexcept(FE_ALL_EXCEPT);
        const bool accepted=wa2::aot::native_grid_binary_snapshot(actual,right,digit);
        CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==host_exceptions && errno==EDOM);
        if(!accepted){CHECK(actual.fp.bytes==original.fp.bytes && actual.r==original.r && actual.eip==original.eip);++native_arithmetic_declined;}
        const auto oracle=digit==0?wr_x87_216_m0:digit==1?wr_x87_216_m1:digit==4?wr_x87_216_m4:wr_x87_216_m6;
        if(accepted){wr::x87_execute(expected,oracle,&right,digit<<3,false,false,0x70000000,true);
        if(actual.fp.bytes!=expected.fp.bytes){std::cerr<<"native grid arithmetic mismatch digit "<<digit<<" case "<<n<<" host rounding "<<rounding<<" left "<<std::hex<<left<<" right "<<right<<std::dec<<'\n';
            for(unsigned at=0;at<actual.fp.bytes.size();++at)if(actual.fp.bytes[at]!=expected.fp.bytes[at])std::cerr<<"FP "<<at<<' '<<unsigned(actual.fp.bytes[at])<<' '<<unsigned(expected.fp.bytes[at])<<'\n';}
        CHECK(actual.fp.bytes==expected.fp.bytes && actual.r==expected.r && actual.flags==expected.flags && actual.defined_flags==expected.defined_flags);++native_arithmetic_accepted;}
        actual=original;expected=original;std::uint32_t stored=0xfacefeed,reference{};
        const bool stored_accepted=wa2::aot::native_grid_stored_binary_snapshot(actual,right,digit,stored);
        CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==host_exceptions && errno==EDOM);
        if(!stored_accepted){CHECK(actual.fp.bytes==original.fp.bytes && actual.eip==original.eip && stored==0xfacefeed);++native_stored_arithmetic_declined;}
        else{
            wr::x87_execute(expected,oracle,&right,digit<<3,false,false,0x70000000,true);expected.eip=0x43d595;
            wr::x87_execute(expected,wr_x87_217_m3,&reference,348,false,false,0x70000004,true);
            CHECK(stored==reference && actual.fp.bytes==expected.fp.bytes && actual.eip==expected.eip && actual.r==expected.r && actual.flags==expected.flags);++native_stored_arithmetic_accepted;
        }
    }
    CHECK(native_arithmetic_accepted>1000 && native_arithmetic_declined>100);
    CHECK(native_stored_arithmetic_accepted>2000 && native_stored_arithmetic_declined>100);
}
struct Snapshot {
    wr::Memory memory;wr::Cpu cpu;
    static constexpr std::uint32_t sp=0x07001000,objects=0xab3fb9;
    explicit Snapshot(bool linear){
        if(linear)CHECK(memory.enable_linear_memory());
        memory.map(0x401000,0xa0000,wr::Memory::Read|wr::Memory::Execute);
        memory.initialize(0x401000,wa2::aot::static_code_bytes);
        for(auto [at,size]:regions)
            memory.map(at,size,wr::Memory::Read|wr::Memory::Write);
    }
    static constexpr std::pair<std::uint32_t,unsigned> regions[]={
        {0x4c2000,4096},{0x07000000,65536},{0xab3000,0x81000},{0x22000000,4096},
        {0x522000,0x5000},{0x4a5000,4096},{0xa94000,4096},{0xa97000,4096},{0xa9e000,4096}};
    void clear(unsigned seed){
        std::mt19937 random(seed);cpu=wr::Cpu{};
        for(auto& r:cpu.r)r=random();cpu.r[wr::ESP]=sp;cpu.flags=random();cpu.defined_flags=random();
        for(auto& byte:cpu.fp.bytes)byte=std::uint8_t(random());
        for(auto [at,size]:regions){
            std::vector<std::uint8_t> bytes(size);for(auto& byte:bytes)byte=std::uint8_t(random());memory.copy_in(at,bytes);
        }
    }
    void original(std::uint32_t end){
        std::uint64_t budget=500000;auto scope=memory.execution_scope();
        while(cpu.eip!=end){wr::tick(budget,cpu.eip);CHECK(shared_aot_oracle(cpu,memory,budget));}
    }
    void same(Snapshot& other){
        if(cpu.fp.bytes!=other.cpu.fp.bytes){unsigned shown{};std::cerr<<"native block end "<<std::hex<<cpu.eip<<std::dec<<'\n';
            for(unsigned n=0;n<cpu.fp.bytes.size() && shown<16;++n)if(cpu.fp.bytes[n]!=other.cpu.fp.bytes[n]){std::cerr<<"FP "<<n<<' '<<unsigned(cpu.fp.bytes[n])<<' '<<unsigned(other.cpu.fp.bytes[n])<<'\n';++shown;}}
        CHECK(cpu.r==other.cpu.r && cpu.eip==other.cpu.eip && cpu.flags==other.cpu.flags && cpu.defined_flags==other.cpu.defined_flags);
        CHECK(cpu.fp.bytes==other.cpu.fp.bytes && cpu.selectors==other.cpu.selectors && cpu.fs_base==other.cpu.fs_base && cpu.gs_base==other.cpu.gs_base && cpu.fs_valid==other.cpu.fs_valid && cpu.gs_valid==other.cpu.gs_valid);
        for(auto [at,size]:regions){
            std::vector<std::uint8_t> left(size),right(size);memory.copy_out(at,left);other.memory.copy_out(at,right);
            if(left!=right){unsigned shown{};for(unsigned n=0;n<size && shown<16;++n)if(left[n]!=right[n]){std::cerr<<"RAM "<<std::hex<<at+n<<std::dec<<' '<<unsigned(left[n])<<' '<<unsigned(right[n])<<'\n';++shown;}}
            CHECK(left==right);
        }
    }
    void sort(unsigned index,unsigned key,unsigned seed){
        clear(seed);cpu.eip=0x442290;cpu.r[wr::EAX]=objects+index*256;cpu.r[wr::EBX]=index-1;
        std::vector<std::uint8_t> zero(0x81000);memory.copy_in(0xab3000,zero);
        memory.store(0x4c22b0,1,32);memory.store(sp+0x1c,cpu.r[wr::EAX],32);
        for(unsigned i=0;i<2048;++i){memory.store(sp+0x438+i*2,i<index?std::uint16_t(-16000+int(i)*16):30000,16);memory.store(sp+0x1438+i*2,0xffff-i,16);}
        constexpr unsigned keys[]={0x8000,0xffff,30000,0,32767,123};
        for(unsigned i=0;i<6 && index+i<2048;++i){
            const auto at=objects+(index+i)*256;memory.store(at-1,1,8);memory.store(at,1,8);memory.store(at+1,i?keys[i]:key,16);memory.store(at+3,i==5?0:1,32);
        }
    }
    void vertex(unsigned mode,unsigned flag,unsigned index,unsigned palette,unsigned seed){
        clear(seed);cpu.eip=0x43bde0;cpu.r[wr::EDI]=index;cpu.r[wr::EDX]=0x30;
        memory.store(sp+0x10,mode,32);memory.store(sp+0x14,flag,32);memory.store(sp+0x48,0x22000000,32);memory.store(sp+0x2c,0x90,32);
        for(unsigned i=0;i<3;++i){memory.store(0x4c22c0+i,palette,8);memory.store(0x4c2320+i,255-palette,8);}
    }
    void uv(bool rows,unsigned precision,unsigned rounding,unsigned seed){
        clear(seed);cpu.eip=rows?0x43cd37:0x43cc4d;cpu.r[wr::ECX]=rounding%3;cpu.r[rows?wr::ESI:wr::EBX]=4;
        cpu.r[wr::EDX]=rounding&1?std::uint32_t(-128):128;
        const auto top=seed%8;cpu.fp=wr::FpState{};
        cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));cpu.fp.put16(2,std::uint16_t(top<<11));
        constexpr float numbers[]={-10,0,1,2,10,20,100,256};
        for(unsigned n=0;n<8;++n){wa2::aot::fp::Environment env;const auto value=wa2::aot::fp::from_binary(std::bit_cast<std::uint32_t>(numbers[(n+precision+rounding)%8]),32,env);
            cpu.fp.put64(32+n*16,value.significand);cpu.fp.put16(40+n*16,value.sign_exponent);
            if(n<5)cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+n)&7));}
        memory.store(sp+0x30,rounding*97-50,32);memory.store(sp+0x68,std::bit_cast<std::uint32_t>(-20.f),32);memory.store(sp+0x6c,std::bit_cast<std::uint32_t>(200.f),32);
    }
    void interpolation(unsigned precision,unsigned rounding,unsigned first,unsigned seed){
        clear(seed);cpu.eip=0x43d3cd;cpu.r[wr::EDX]=first;cpu.r[wr::EBX]=4;cpu.r[wr::ECX]=rounding*0x44;
        const auto top=seed%8;cpu.fp=wr::FpState{};cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));cpu.fp.put16(2,std::uint16_t(top<<11));
        for(unsigned n=0;n<8;++n){wa2::aot::fp::Environment env;const auto value=wa2::aot::fp::from_integer((n+1)*32);
            cpu.fp.put64(32+n*16,value.significand);cpu.fp.put16(40+n*16,value.sign_exponent);
            if(n<5)cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+n)&7));}
        const std::pair<unsigned,float> inputs[]={{0x14,.5f},{0x178,100},{0x30,20},{0x1b8,63.5f},{0x4c,128.25f},
            {0x150,64},{0x48,16.5f},{0x1a8,-128},{0x2c,255.5f},{0x158,-200},{0x28,100.75f},
            {0xa8,20.25f},{0x7c,100.75f},{0xa0,15.75f},{0x18,120.5f},{0x198,-50.25f},{0x10,255.25f}};
        for(auto [offset,value]:inputs)memory.store(sp+offset,std::bit_cast<std::uint32_t>(value),32);
        // Cached operand widths distinguish the binary64 differences from
        // the binary32 bases/output fields in this interpolation function.
        for(auto [offset,value]:inputs)if(offset==0x178 || offset==0x1b8 || offset==0x150 || offset==0x1a8 || offset==0x158 || offset==0xa8 || offset==0xa0 || offset==0x198){
            const auto bits=std::bit_cast<std::uint64_t>(double(value));memory.store(sp+offset,std::uint32_t(bits),32);memory.store(sp+offset+4,std::uint32_t(bits>>32),32);}
        for(unsigned n=0;n<=4;++n){memory.store(sp+0x2f4+n*4,std::bit_cast<std::uint32_t>(100.f+n*32),32);memory.store(sp+0x264+n*4,std::bit_cast<std::uint32_t>(-50.f+n*16),32);}
    }
    void grid(unsigned precision,unsigned rounding,unsigned count,unsigned seed){
        clear(seed);cpu.eip=0x43ce95;cpu.r[wr::EBX]=count;
        const auto top=seed%8;cpu.fp=wr::FpState{};
        cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));cpu.fp.put16(2,std::uint16_t((top<<11)|(seed&0x3f)));
        for(unsigned n=0;n<8;++n){const auto value=wa2::aot::fp::from_integer(n?20+n*30:0);
            cpu.fp.put64(32+n*16,value.significand);cpu.fp.put16(40+n*16,value.sign_exponent);
            if(n<3)cpu.fp.bytes[4]|=std::uint8_t(1u<<((top+n)&7));}
        memory.store(sp+0x34,0x22000000,32);memory.store(sp+0x78,count,32);
        memory.store(0x22000060,256,16);memory.store(0x22000062,256,16);
        for(unsigned at:{0x68u,0x70u,0x80u,0x84u,0x90u,0x94u})memory.store(sp+at,std::bit_cast<std::uint32_t>(20.25f),32);
        for(unsigned at:{0x6cu,0x74u,0x88u,0x8cu,0x98u,0x9cu})memory.store(sp+at,std::bit_cast<std::uint32_t>(300.75f),32);
        for(unsigned n=0;n<17;++n)for(unsigned at:{0x268u,0x2f8u,0x340u,0x2b0u})
            memory.store(sp+at+n*4,std::bit_cast<std::uint32_t>((at==0x268 || at==0x340?0.f:128.f)+n*256),32);
    }
    void clipped_grid(unsigned precision,unsigned rounding,unsigned count,bool crop,unsigned seed){
        grid(precision,rounding,count,seed);cpu.eip=0x43cc17;cpu.r[wr::ESP]-=0x30;cpu.r[wr::EDI]=0x22000000;cpu.fp.bytes[4]=0;
        memory.store(0x22000064,count-1,16);memory.store(0x22000066,count-1,16);memory.store(sp+0x64,0x22000100,32);
        memory.store(0x220001dc,crop?1:0,32);
        for(unsigned n=0;n<8;++n)memory.store(0x220001e0+n*4,std::bit_cast<std::uint32_t>(float(n*31+17)),32);
    }
    void snow_motion(unsigned variant,unsigned seed){
        clear(seed);cpu.fp=wr::FpState{};cpu.eip=0x4089c5;cpu.r[wr::ESI]=(variant%400)*40;
        const auto top=seed%8;cpu.fp.put16(0,0x7f);cpu.fp.put16(2,std::uint16_t((top<<11)|(seed&0x473f)));
        cpu.fp.bytes[4]=std::rotl(std::uint8_t(7),int(top));
        constexpr float values[]={40.f,10.f,1.f/4096.f,0.f,17.f,-3.f,8.f,100.f};
        for(unsigned n=0;n<8;++n){wa2::aot::fp::Environment e;const auto v=wa2::aot::fp::from_binary(std::bit_cast<std::uint32_t>(values[n]),32,e);
            cpu.fp.put64(32+n*16,v.significand);cpu.fp.put16(40+n*16,v.sign_exponent);}
        const auto record=0x522948+cpu.r[wr::ESI];const auto f=[&](unsigned at,float v){memory.store(at,std::bit_cast<std::uint32_t>(v),32);};
        constexpr int phases[]={0,1,63,127,255,-65,-257,-32768,32767};const auto phase=phases[variant%std::size(phases)];
        memory.store(record+8,std::uint16_t(phase),16);memory.store(0x4a5b28+((phase+64)%256)*2,variant*53-1024,16);
        f(record+12,variant&1?-28.25f:241.5f);f(record+16,variant&2?-20.f:400.25f);
        f(record+24,variant&4?-.75f:1.25f);f(record+28,variant&8?.5f:2.25f);
        f(0x5267cc,variant&16?-3.25f:1.5f);f(0x5267d0,variant&32?0.f:3.f);
    }
    void sprite_geometry(unsigned variant,unsigned seed){
        clear(seed);cpu.eip=0x43bba8;cpu.r[wr::ESI]=0x22000000;
        cpu.fp=wr::FpState{};
        for(unsigned n=0;n<8;++n){const auto value=wa2::aot::fp::from_integer(std::int32_t(seed+n*17)-64);
            cpu.fp.put64(32+n*16,value.significand);cpu.fp.put16(40+n*16,value.sign_exponent);}
        cpu.fp.put16(0,0x7f);cpu.fp.put16(2,std::uint16_t(((seed%8)<<11)|(seed&0x473f)));cpu.fp.bytes[4]=0;
        memory.store(0x22000044,variant&3,32);memory.store(0x22000052,std::uint8_t(variant*47),8);
        const auto f=[&](unsigned at,float value){memory.store(at,std::bit_cast<std::uint32_t>(value),32);};
        f(sp+0x10,variant&1?.6f:-.3f);f(sp+0x18,variant&1?.8f:.7f);
        f(0x22000078,-21.25f);f(0x2200007c,32.5f);f(0x22000080,variant&4?-.75f:1.125f);f(0x22000084,.9f);
        for(unsigned n=0;n<4;++n){f(0x22000088+n*4,float(n*53)-51.25f);f(0x22000098+n*4,float(n*31)-9.5f);}
        f(0x220000bc,variant&2?-18.5f:20.25f);f(0x220000c4,variant&8?-3.75f:0.f);
    }
};
inline unsigned run(){
    arithmetic();
    unsigned cases{};
    // Small bounded snapshots only; no repeated frame loop or timing test.
    for(bool linear:{false,true}){
        Snapshot native(linear),original(linear);
        for(unsigned variant=0;variant<256;++variant){
            for(auto* snapshot:{&native,&original}){
                snapshot->clear(variant*491+31);snapshot->cpu.eip=0x43b290;
                const auto stack=Snapshot::sp+(variant%5)*4,index=81+(variant%4)*200;snapshot->cpu.r[wr::ESP]=stack;
                snapshot->memory.store(stack,0xdeadbeef,32);snapshot->memory.store(stack+4,index,32);snapshot->memory.store(stack+8,variant%4,32);
                constexpr int alpha[]={-2147483647-1,-1,0,1,127,128,254,255,256,2147483647};
                snapshot->memory.store(stack+12,std::uint32_t(alpha[variant%std::size(alpha)]),32);
                constexpr unsigned edge[]={0,1,127,128,129,254,255};
                snapshot->memory.store(stack+16,variant<98?edge[(variant%49)/7]:(variant*97)&255,32);
                snapshot->memory.store(stack+20,variant<98?edge[variant%7]:(variant*47)&255,32);
                snapshot->memory.store(stack+24,variant<98?edge[(variant*3)%7]:(variant*151)&255,32);
                snapshot->memory.store(0xab4002+index*256,(variant<98?variant>=49:bool(variant&4))?0xffff:110,16);
            }
            CHECK(wa2::aot::native_weather_color_snapshot(native.cpu,native.memory));original.original(0xdeadbeef);native.same(original);++cases;
        }
        for(unsigned variant=0;variant<64;++variant){
            native.snow_motion(variant,variant*37+11);original.snow_motion(variant,variant*37+11);
            CHECK(wa2::aot::native_snow_motion(native.cpu,native.memory));original.original(0x408a37);native.same(original);++cases;
        }
        for(unsigned reason=0;reason<7;++reason){
            native.snow_motion(3,11);original.snow_motion(3,11);
            for(auto* snapshot:{&native,&original}){
                if(reason==0)snapshot->cpu.fp.put16(0,0x37f);
                if(reason==1)snapshot->cpu.fp.bytes[4]=0;
                if(reason==2)snapshot->memory.store(0x522948+snapshot->cpu.r[wr::ESI]+24,0x7fc00123,32);
                if(reason==3)snapshot->memory.store(0x5267d0,1,32);
                if(reason==4)snapshot->cpu.r[wr::ESI]=400*40;
                if(reason==5)snapshot->memory.protect(Snapshot::sp,4096,wr::Memory::Read);
                if(reason==6)snapshot->cpu.r[wr::ESP]=0x522948+snapshot->cpu.r[wr::ESI];
            }
            CHECK(!wa2::aot::native_snow_motion(native.cpu,native.memory));native.same(original);++cases;
            if(reason==5)for(auto* snapshot:{&native,&original})snapshot->memory.protect(Snapshot::sp,4096,wr::Memory::Read|wr::Memory::Write);
        }
        for(unsigned variant=0;variant<32;++variant){
            native.sprite_geometry(variant,variant*37+11);original.sprite_geometry(variant,variant*37+11);
            CHECK(wa2::aot::native_sprite_geometry(native.cpu,native.memory));original.original(0x43bde0);native.same(original);++cases;
        }
        for(unsigned reason=0;reason<6;++reason){
            native.sprite_geometry(1,11);original.sprite_geometry(1,11);
            for(auto* snapshot:{&native,&original}){
                if(reason==0)snapshot->cpu.fp.put16(0,0x37f);
                if(reason==1)snapshot->cpu.fp.bytes[4]=1;
                if(reason==2)snapshot->memory.store(0x22000080,0x7fc00123,32);
                if(reason==3)snapshot->memory.store(0x22000088,1,32);
                if(reason==4)snapshot->cpu.r[wr::ESI]=Snapshot::sp;
                if(reason==5)snapshot->memory.protect(Snapshot::sp,4096,wr::Memory::Read);
            }
            CHECK(!wa2::aot::native_sprite_geometry(native.cpu,native.memory));native.same(original);++cases;
            if(reason==5)for(auto* snapshot:{&native,&original})snapshot->memory.protect(Snapshot::sp,4096,wr::Memory::Read|wr::Memory::Write);
        }
        for(unsigned index:{0u,31u,2047u})for(unsigned key:{0x8000u,0xffffu,30000u}){
            native.sort(index,key,index+key);original.sort(index,key,index+key);
            CHECK(wr::wa2_sort_objects_full(native.cpu,native.memory));original.original(0x442374);native.same(original);++cases;
        }
        for(unsigned mode:{0u,1u})for(unsigned flag:{0u,1u})for(unsigned index:{0u,12u})for(unsigned palette:{0u,129u,255u}){
            const auto seed=mode*31+flag*7+index+palette;
            native.vertex(mode,flag,index,palette,seed);original.vertex(mode,flag,index,palette,seed);
            CHECK(wr::wa2_vertex_full(native.cpu,native.memory));original.original(0x43c100);native.same(original);++cases;
        }
        for(bool rows:{false,true})for(unsigned precision:{0u,2u,3u})for(unsigned rounding:{0u,1u,2u,3u}){
            const auto seed=precision*17+rounding*3+rows;
            native.uv(rows,precision,rounding,seed);original.uv(rows,precision,rounding,seed);
            CHECK(rows?wa2::aot::native_uv_rows(native.cpu,native.memory):wa2::aot::native_uv_columns(native.cpu,native.memory));
            original.original(rows?0x43cdf0:0x43cd06);native.same(original);++cases;
        }
        for(unsigned reason:{0u,1u,2u}){
            native.uv(false,3,0,7);original.uv(false,3,0,7);
            for(auto* snapshot:{&native,&original}){
                if(reason==0)snapshot->cpu.r[wr::EBX]=33;
                if(reason==1)snapshot->cpu.fp.bytes[4]=0;
                if(reason==2)snapshot->cpu.fp.put16(0,0x37e);
            }
            CHECK(!wa2::aot::native_uv_columns(native.cpu,native.memory));native.same(original);++cases;
        }
        for(unsigned precision:{0u,2u,3u})for(unsigned rounding:{0u,1u,2u,3u})for(unsigned first:{0u,1u}){
            const auto seed=precision*11+rounding*3+first;
            native.interpolation(precision,rounding,first,seed);original.interpolation(precision,rounding,first,seed);
            CHECK(wa2::aot::native_vertex_interpolation(native.cpu,native.memory));original.original(0x43d59f);native.same(original);++cases;
        }
        for(unsigned reason:{0u,1u,2u}){
            native.interpolation(0,0,0,11);original.interpolation(0,0,0,11);
            for(auto* snapshot:{&native,&original}){if(reason==0)snapshot->cpu.r[wr::EBX]=33;if(reason==1)snapshot->cpu.r[wr::ECX]=0x501;if(reason==2)snapshot->cpu.fp.bytes[4]=0;}
            CHECK(!wa2::aot::native_vertex_interpolation(native.cpu,native.memory));native.same(original);++cases;
        }
        for(unsigned precision:{0u,2u,3u})for(unsigned rounding:{0u,1u,2u,3u})for(unsigned count:{0u,2u,3u,17u}){
            const auto seed=precision*13+rounding*7+count;
            native.grid(precision,rounding,count,seed);original.grid(precision,rounding,count,seed);
            CHECK(wa2::aot::native_vertex_grid(native.cpu,native.memory));original.original(0x43d5d7);native.same(original);++cases;
        }
        for(unsigned reason:{0u,1u,2u,3u}){
            native.grid(0,0,3,3);original.grid(0,0,3,3);
            for(auto* snapshot:{&native,&original}){
                if(reason==0)snapshot->cpu.r[wr::EBX]=18;
                if(reason==1)snapshot->memory.store(Snapshot::sp+0x78,18,32);
                if(reason==2)snapshot->memory.store(Snapshot::sp+0x34,Snapshot::sp,32);
                if(reason==3)snapshot->cpu.fp.put16(0,0x7e);
            }
            CHECK(!wa2::aot::native_vertex_grid(native.cpu,native.memory));native.same(original);++cases;
        }
        // A direct grid resume can carry sparse incoming tags. In an empty
        // grid, untouched holes must stay holes instead of becoming live slots.
        for(unsigned top=0;top<8;++top)for(unsigned tags=0;tags<8;++tags){
            native.grid(0,0,0,top);original.grid(0,0,0,top);
            for(auto* snapshot:{&native,&original})snapshot->cpu.fp.bytes[4]=std::rotl(std::uint8_t(tags),int(top));
            CHECK(wa2::aot::native_vertex_grid(native.cpu,native.memory));original.original(0x43d5d7);native.same(original);++cases;
        }
        for(unsigned precision:{0u,2u,3u})for(unsigned rounding:{0u,1u,2u,3u})for(unsigned count:{2u,4u})for(bool crop:{false,true}){
            const auto seed=precision*13+rounding*7+count+crop;
            native.clipped_grid(precision,rounding,count,crop,seed);original.clipped_grid(precision,rounding,count,crop,seed);
            CHECK(wa2::aot::native_vertex_grid(native.cpu,native.memory));original.original(0x43d5d7);native.same(original);++cases;
        }
        // Unsorted source keys are outside this port's contract. A decline
        // must preserve all state so the same compiled function can continue.
        native.sort(31,1,7);original.sort(31,1,7);
        for(auto* s:{&native,&original}){s->memory.store(Snapshot::sp+0x438,30000,16);s->memory.store(Snapshot::sp+0x43a,0x8000,16);}
        CHECK(!wr::wa2_sort_objects_full(native.cpu,native.memory));native.same(original);++cases;
    }
    return cases;
}
}
