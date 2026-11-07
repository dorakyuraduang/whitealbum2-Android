#pragma once
// Purely constructed LSCR/PAK/font-empty fixtures; no original resources.
#include "scene.hpp"
#include "saved_scene.hpp"
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace g=wa2::game;namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::progress;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
void put(Bytes& b,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
void word(Bytes& b,unsigned value){const auto at=b.size();b.resize(at+4);put(b,at,value);}
Bytes text(std::string_view value){return {value.begin(),value.end()};}
struct Source:r::Source {Bytes b;std::uint64_t size()const override{return b.size();}void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}};
std::shared_ptr<Source> bank(std::map<std::string,Bytes> entries){auto source=std::make_shared<Source>();auto& b=source->b;b.resize(16+entries.size()*44);std::memcpy(b.data(),"KCAP",4);put(b,12,unsigned(entries.size()));unsigned n{};for(const auto& [name,data]:entries){CHECK(name.size()<32);std::memcpy(b.data()+20+n*44,name.data(),name.size());put(b,52+n*44,unsigned(b.size()));put(b,56+n*44,unsigned(data.size()));b.insert(b.end(),data.begin(),data.end());++n;}return source;}
Bytes raster(unsigned width,unsigned height){Bytes b(18+width*height*4);b[2]=2;b[12]=std::uint8_t(width);b[13]=std::uint8_t(width>>8);b[14]=std::uint8_t(height);b[15]=std::uint8_t(height>>8);b[16]=32;b[17]=40;for(unsigned n=0;n<width*height;++n){b[18+n*4]=31;b[19+n*4]=57;b[20+n*4]=191;b[21+n*4]=255;}return b;}
Bytes wave(){Bytes b(52);std::memcpy(b.data(),"RIFF",4);put(b,4,44);std::memcpy(b.data()+8,"WAVEfmt ",8);put(b,16,16);b[20]=b[22]=1;put(b,24,1000);put(b,28,2000);b[32]=2;b[34]=16;std::memcpy(b.data()+36,"data",4);put(b,40,8);return b;}
const std::map<unsigned,std::vector<std::uint8_t>> signatures{{0,{5,3}},{1,{5,3}},{2,{3}},{8,{3}},{131,{5,3,3}},{133,{5,3,3}},{136,{3,3}},{138,{3,3,3,3,3}},{139,{3,3,3,3,3,3}},{146,{3,3,3,3,3,3,3,4,4}},{147,{3,3,3,3,3,3,3,4,4}},{148,{3,3,3,3,3,3,3,4,4}},{149,{3,3,3,3,3,3,3,4,4}},{150,{3,3,3,3,3}},{154,{3,3,3,3,3,3,3,3}},{155,{3,3,3,3,3,3}},{158,{3,3,3,3}},{164,{3,3}},{165,{3,3,3,3,3,3}},{170,{3}},{171,{3,3}},{172,{5,3}},{176,{3,5,3,3,5,3,5,3}},{185,{3,3,3,3}},{193,{5,3}},{194,{3,3,3}},{208,{5,3,3,3}},{209,{3}},{225,{3,3,3,3,3,3,3,4,4,4,3}},{226,{3,3,3,3,3,3,3,4,4,4,3}},{227,{3,3,3,3,3,3,3,4,4,4,3}},{228,{3,3,3,3,3,3,3,4,4,4,3}},{229,{3,3,3,3,3,3,3}}};
Bytes functions(unsigned first,unsigned last){Bytes b;word(b,first);for(unsigned id=first;id<last;++id){const auto at=b.size();b.resize(at+120);b[at]='f';put(b,at+36,3);if(auto found=signatures.find(id);found!=signatures.end()){b[at+32]=std::uint8_t(found->second.size());std::copy(found->second.begin(),found->second.end(),b.begin()+at+40);for(auto unused:found->second)b.push_back(',');}}return b;}
struct Script {Bytes code{'L','S','C','R'};std::string strings;unsigned string_count{};Script(){word(code,20);word(code,1);word(code,0);word(code,20);}void call(unsigned id,std::initializer_list<s::Value> values){for(const auto& value:values){if(value.type()==s::Type::text){word(code,3);word(code,string_count++);strings+=value.text();strings+=',';}else{word(code,5);word(code,unsigned(value.type()));word(code,value.type()==s::Type::real?std::bit_cast<unsigned>(value.real()):std::bit_cast<unsigned>(value.integer()));}}word(code,4);word(code,id);word(code,6);word(code,30);}void finish(){word(code,0);word(code,1);}};
std::shared_ptr<Source> scripts(bool changed=false){Script script;script.call(8,{s::Value(changed?51:50)});script.call(171,{s::Value(256),s::Value(0)});script.call(146,{s::Value(1),s::Value(1),s::Value(0),s::Value(8),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)});
    script.call(176,{s::Value(0),s::Value(std::string("atlas.tga")),s::Value(14),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(0)});script.call(185,{s::Value(0),s::Value(4),s::Value(90),s::Value(30)});script.call(154,{s::Value(10),s::Value(1),s::Value(1),s::Value(0),s::Value(0),s::Value(12),s::Value(255),s::Value(128)});
    script.call(229,{s::Value(0x103),s::Value(0),s::Value(10),s::Value(10),s::Value(0),s::Value(1),s::Value(5)});script.call(150,{s::Value(9),s::Value(2),s::Value(0),s::Value(0),s::Value(100)});script.call(194,{s::Value(100),s::Value(0),s::Value(0)});script.call(131,{s::Value(std::string("ABC\\kDEF")),s::Value(7),s::Value(1)});
    for(unsigned n=0;n<2;++n)script.call(208,{s::Value(std::string()),s::Value(0),s::Value(0),s::Value(0)});script.call(209,{s::Value(0)});script.call(194,{s::Value(20),s::Value(0),s::Value(0)});script.finish();return bank({{"Global.vrb",text("3,FLG_00,0\r\n")},{"LFScriptFunc.fnc",functions(0,200)},{"LFScriptFuncEx.fnc",functions(200,239)},{"1001.bnr",std::move(script.code)},{"1001.txt",text(script.strings)}});}
struct Storage:p::Storage {std::optional<Bytes> bytes;unsigned writes{};std::optional<Bytes> load()override{return bytes;}void write(p::Block block,std::span<const std::uint8_t> data)override{if(!bytes)bytes=Bytes(p::layout::system_save_bytes);std::copy(data.begin(),data.end(),bytes->begin()+p::offset(block));++writes;}};
inline std::map<std::string,std::shared_ptr<Source>> memory_banks(){
    std::map<std::string,std::shared_ptr<Source>> banks;
    banks["fon.pak"]=bank({});banks["ck-gal.pak"]=scripts();banks["bak.pak"]=bank({{"B000100.tga",raster(64,32)}});banks["char.pak"]=bank({{"tak000001.tga",raster(32,64)}});banks["grp.pak"]=bank({{"atlas.tga",raster(32,16)},{"weather.tga",raster(32,64)},{"sys_00000.tga",raster(1,1)},{"sys_00001.tga",raster(1,1)},{"sys_00010.tga",raster(1,1)},{"sys_00011.tga",raster(248,104)},{"sys_00200.tga",raster(784,216)},{"sys_00301.tga",raster(192,8)},{"sys_00300.tga",raster(264,1008)}});
    std::map<std::string,Bytes> cues;for(auto id:{"9015","9211","9212","9213","9214"})cues[std::string("SE_")+id+".wav"]=wave();banks["se.pak"]=bank(std::move(cues));return banks;
}
inline g::Scene::SavedImageResolver memory_resolver(){return [](r::Catalog& resources)->i::Resolve{return [&resources](const i::Reference& ref){auto request=resources.image(ref);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Synthetic scene image wait expired");};};}
