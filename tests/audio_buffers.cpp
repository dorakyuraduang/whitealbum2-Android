// Replay the real 52,992,000-byte SE_9714.WAV Lock request. No windows,
// screen access or game input; playback is muted at the buffer volume.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <fstream>
#include <iostream>
#include <random>
#include "support.hpp"
#include "wa2_win32.hpp"
#include <winrecomp/hash.hpp>
using wr::U32;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
static bool step(wr::Cpu&,wr::Memory&,std::uint64_t&){return false;}
static U32 get32(const std::uint8_t* b){return U32(b[0])|(U32(b[1])<<8)|(U32(b[2])<<16)|(U32(b[3])<<24);}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto root=std::filesystem::absolute(argv[1]).parent_path();
    std::ifstream file(root/"res/SE.PAK",std::ios::binary);CHECK(file);
    std::array<std::uint8_t,8> header{};file.read(reinterpret_cast<char*>(header.data()),8);
    CHECK((std::string(reinterpret_cast<const char*>(header.data()),4)==std::string("LAC\0",4)));
    U32 offset=0,length=0;
    for(U32 n=0;n<get32(header.data()+4);++n){std::array<std::uint8_t,40> row{};file.read(reinterpret_cast<char*>(row.data()),40);CHECK(file);
        std::string name;for(unsigned k=0;k<31 && row[k];++k)name+=char(row[k]^255);
        if(name=="SE_9714.WAV"){length=get32(row.data()+32);offset=get32(row.data()+36);break;}}
    CHECK(length==52993168 && offset==787134074);
    std::vector<std::uint8_t> wave(length);file.seekg(offset);file.read(reinterpret_cast<char*>(wave.data()),wave.size());CHECK(file);
    CHECK(std::string(reinterpret_cast<char*>(wave.data()),4)=="RIFF");
    U32 pcm_offset=0,pcm_length=0;std::vector<std::uint8_t> format;
    for(std::size_t at=12;at+8<=wave.size();){const auto bytes=get32(wave.data()+at+4);CHECK(bytes<=wave.size()-at-8);
        const auto id=std::string(reinterpret_cast<char*>(wave.data()+at),4);
        if(id=="fmt "){format.assign(wave.begin()+at+8,wave.begin()+at+8+bytes);if(format.size()==16)format.resize(18);}
        if(id=="data"){pcm_offset=U32(at+8);pcm_length=bytes;}at+=8+bytes+(bytes&1);}
    CHECK(pcm_length==52992000 && pcm_offset==1024 && format.size()==18);
    const auto actual=std::span(wave).subspan(pcm_offset,pcm_length);const auto expected=wr::sha256(actual);
    wr::ProcessOptions options;options.data_root=root/"res";options.instruction_budget=UINT64_MAX;
    wr::Process p(step,options);wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    const auto scratch=p.allocate_bytes(4096);p.memory.copy_in(scratch+512,format);
    auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){const auto at=p.resolve(dll,name);CHECK(at);return p.callback(at,std::span(args.begin(),args.size()));};
    auto com=[&](U32 object,unsigned slot,std::initializer_list<U32> args){std::vector<U32> words{object};words.insert(words.end(),args.begin(),args.end());return p.callback(p.memory.load(p.memory.load(object,32)+slot*4,32),words);};
    auto ok=[](U32 hr){CHECK((hr&0x80000000u)==0);};
    ok(call("dsound.dll","#11",{0,scratch,0}));const auto device=p.memory.load(scratch,32);CHECK(device);
    const auto flags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLPOSITIONNOTIFY|DSBCAPS_GETCURRENTPOSITION2;
    auto create=[&](U32 bytes){wr::port::write(p,scratch+256,std::array<U32,9>{36,U32(flags),bytes,0,scratch+512,0,0,0,0});ok(com(device,3,{scratch+256,scratch,0}));return p.memory.load(scratch,32);};
    auto lock=[&](U32 object,U32 start,U32 bytes,U32 mode=0){ok(com(object,11,{start,bytes,scratch+16,scratch+20,scratch+24,scratch+28,mode}));return wr::port::read<std::array<U32,4>>(p,scratch+16);};
    auto unlock=[&](U32 object,const std::array<U32,4>& parts){ok(com(object,19,{parts[0],parts[1],parts[2],parts[3]}));};
    // Warm vtables before recording allocations, including the 8 and notify interfaces.
    const auto warm=create(4096);wr::port::write(p,scratch+128,IID_IDirectSoundBuffer8);ok(com(warm,0,{scratch+128,scratch}));com(p.memory.load(scratch,32),2,{});
    wr::port::write(p,scratch+128,IID_IDirectSoundNotify);ok(com(warm,0,{scratch+128,scratch}));com(p.memory.load(scratch,32),2,{});CHECK(com(warm,2,{})==0);
    const auto baseline=p.memory.regions().size();unsigned cycles=0;
    for(unsigned n=0;n<8;++n){
        const auto buffer=create(pcm_length);ok(com(buffer,15,{U32(DSBVOLUME_MIN)}));
        auto parts=lock(buffer,0,pcm_length);CHECK(parts[1]==pcm_length && !parts[2] && !parts[3]);
        p.memory.copy_in(parts[0],actual);unlock(buffer,parts);CHECK(p.state().reports.at("directsound")().find("\"staging_bytes\":0,")!=std::string::npos);
        parts=lock(buffer,0,0,DSBLOCK_ENTIREBUFFER);std::vector<std::uint8_t> readback(pcm_length);p.memory.copy_out(parts[0],readback);CHECK(wr::sha256(readback)==expected);unlock(buffer,parts);
        // Ring wrapping preserves both ends, including non-frame-sized writes.
        parts=lock(buffer,pcm_length-7,16);CHECK(parts[1]==7 && parts[3]==9);for(U32 k=0;k<7;++k)p.memory.store(parts[0]+k,0x91,8);for(U32 k=0;k<9;++k)p.memory.store(parts[2]+k,0x72,8);unlock(buffer,parts);
        parts=lock(buffer,pcm_length-7,16);CHECK(p.memory.load(parts[0],8)==0x91 && p.memory.load(parts[2],8)==0x72);unlock(buffer,parts);
        ok(com(buffer,12,{0,0,DSBPLAY_LOOPING}));ok(com(buffer,18,{}));CHECK(com(buffer,2,{})==0);
        CHECK(p.memory.regions().size()==baseline);++cycles;
    }
    const auto b=create(4096);
    CHECK(com(b,11,{4096,4,scratch+16,scratch+20,scratch+24,scratch+28,0})==U32(DSERR_INVALIDPARAM));
    CHECK(com(b,11,{0,4097,scratch+16,scratch+20,scratch+24,scratch+28,0})==U32(DSERR_INVALIDPARAM));
    auto parts=lock(b,4000,256);CHECK(parts[1]==96 && parts[3]==160);
    CHECK(com(b,11,{0,4,scratch+16,scratch+20,scratch+24,scratch+28,0})==U32(DSERR_INVALIDCALL));
    CHECK(com(b,19,{parts[0]+1,parts[1],parts[2],parts[3]})==U32(DSERR_INVALIDPARAM));unlock(b,parts);
    CHECK(com(b,19,{parts[0],parts[1],parts[2],parts[3]})==U32(DSERR_INVALIDCALL));CHECK(com(b,2,{})==0);CHECK(p.memory.regions().size()==baseline);
    CHECK(com(device,2,{})==0);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.audio-buffers.v1\",\"real_resource\":\"SE.PAK/SE_9714.WAV\",\"resource_offset\":"<<offset<<",\"resource_bytes\":"<<length<<",\"pcm_bytes\":"<<pcm_length<<",\"pcm_sha256\":\""<<expected<<"\",\"large_load_release_cycles\":"<<cycles<<",\"regions_restored_after_every_cycle\":true,\"full_pcm_round_trip\":true,\"screen_access\":false,\"gameplay_verified\":false,\"passed\":true,\"audio\":"<<p.state().reports.at("directsound")()<<"}\n";CHECK(report);
    std::cout<<"PASS actual SE_9714.WAV: "<<cycles<<" large PCM load/readback/release cycles, wrapping and invalid locks, full hashes and region lifetime; no windows or screen access\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
