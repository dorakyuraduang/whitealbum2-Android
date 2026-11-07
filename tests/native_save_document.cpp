#include "save_document.hpp"
#include "save_slots.hpp"
#include "save_storage.hpp"
#include "private_file.hpp"
#include "checkpoint_codec.hpp"
#include "save_catalog.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <condition_variable>
#include <atomic>

namespace g=wa2::game;namespace i=wa2::image;namespace c=wa2::checkpoint;namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
g::SavedScene fixture(){g::SavedScene value;value.scene="1001";value.random=17;value.ticks=18;for(unsigned n=0;n<value.parts.size();++n){c::Writer part;part.u32(n);if(!n)part.blob(Bytes(160u*1024,33));value.parts[n]=part.seal(g::scene_part_tags[n]);}return value;}
struct Memory:g::SaveStorage {
    std::map<unsigned,Bytes> files;unsigned full_reads{},prefix_reads{},writes{};std::size_t largest_request{};bool fail{};
    std::optional<Bytes> read(unsigned slot)override{++full_reads;const auto it=files.find(slot);return it==files.end()?std::optional<Bytes>{}:it->second;}
    std::optional<Bytes> prefix(unsigned slot,std::size_t count)override{++prefix_reads;largest_request=std::max(largest_request,count);const auto it=files.find(slot);if(it==files.end())return {};return Bytes(it->second.begin(),it->second.begin()+std::min(count,it->second.size()));}
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{if(fail)throw std::runtime_error("owned save write failure");files[slot]=Bytes(bytes.begin(),bytes.end());++writes;}
    std::vector<g::SaveSlotInfo> list()override{std::vector<g::SaveSlotInfo> result;for(const auto& [slot,bytes]:files)result.push_back({slot,bytes.size(),42});return result;}
};
void file(const fs::path& path,std::span<const std::uint8_t> bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));CHECK(out);}
std::uint32_t reference_crc(std::span<const std::uint8_t> bytes){std::uint32_t value=0xffffffff;for(auto byte:bytes){value^=byte;for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&std::uint32_t(0-int(value&1)));}return ~value;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const std::string known="123456789";CHECK(c::crc({reinterpret_cast<const std::uint8_t*>(known.data()),known.size()})==0xcbf43926u && c::crc({})==0);++cases;
    Bytes sequence(4097);for(unsigned n=0;n<sequence.size();++n)sequence[n]=std::uint8_t((n*37)^n/7);for(unsigned length:{1u,7u,16u,255u,256u,4096u,4097u})CHECK(c::crc(std::span(sequence).first(length))==reference_crc(std::span(sequence).first(length)));++cases;
    auto picture=std::make_shared<i::Raster>(i::Raster{128,72,Bytes(128u*72*4)});for(unsigned n=0;n<picture->rgba.size();++n)picture->rgba[n]=std::uint8_t(n);
    g::SavePreview preview;preview.scene="1001";preview.description="ABC";preview.saved_ms=951827696000ull;preview.date={2000,2,29,2,12,34,56};preview.thumbnail=picture;
    const auto metadata=preview.encode();const auto decoded=g::SavePreview::decode(metadata);CHECK(decoded.scene==preview.scene && decoded.description==preview.description && decoded.date==preview.date && decoded.saved_ms==preview.saved_ms && decoded.thumbnail->rgba==picture->rgba);++cases;
    g::SaveDocument document{preview,fixture().encode()};const auto bytes=document.encode();CHECK(g::SaveDocument::wrapped(bytes) && bytes.size()>g::SaveDocument::prefix_bytes);const auto restored=g::SaveDocument::decode(bytes);CHECK(restored.snapshot==document.snapshot && restored.preview.thumbnail->rgba==picture->rgba);++cases;
    const auto prefix=std::span(bytes).first(g::SaveDocument::prefix_bytes);const auto inspected=g::SaveDocument::inspect(prefix);CHECK(inspected.scene==preview.scene && inspected.thumbnail->rgba==picture->rgba);++cases;
    auto damaged=bytes;damaged.back()^=1;CHECK(g::SaveDocument::inspect(std::span(damaged).first(g::SaveDocument::prefix_bytes)).description==preview.description);rejects([&]{g::SaveDocument::decode(damaged);});++guards;++cases;
    damaged=bytes;damaged[20]^=1;rejects([&]{g::SaveDocument::inspect(damaged);});++guards;
    for(unsigned length:{0u,3u,4u,8u,11u,12u,24u,unsigned(12+metadata.size()-1)}){rejects([&]{g::SaveDocument::inspect(std::span(bytes).first(length));});++guards;}++cases;
    damaged=bytes;damaged[4]=2;rejects([&]{g::SaveDocument::decode(damaged);});++guards;damaged=bytes;for(unsigned n=8;n<12;++n)damaged[n]=255;rejects([&]{g::SaveDocument::inspect(damaged);});++guards;damaged=bytes;damaged.push_back(0);rejects([&]{g::SaveDocument::decode(damaged);});++guards;++cases;
    auto mismatched=document;mismatched.preview.scene="2001";rejects([&]{mismatched.encode();});++guards;
    for(auto date:std::array<g::SaveDate,10>{{{0,1,1},{10000,1,1},{2025,2,29},{2000,257,1},{2000,1,257},{2000,1,1,7},{2000,1,1,1,24},{2000,1,1,1,0,60},{2000,1,1,1,0,0,60},{2000,0,1}}}){rejects([&]{date.validate();});++guards;}++cases;
    for(unsigned n=0;n<7;++n){auto invalid=preview;if(n==0)invalid.scene="../escape";if(n==1)invalid.scene.clear();if(n==2)invalid.description=std::string(257,'a');if(n==3)invalid.description=std::string("a\0b",3);if(n==4)invalid.saved_ms=253402300800000ull;if(n==5)invalid.thumbnail=std::make_shared<i::Raster>(i::Raster{1,1,{1,2,3,4}});if(n==6)invalid.legacy=true;rejects([&]{invalid.encode();});++guards;}++cases;
    auto no_picture=preview;no_picture.thumbnail.reset();CHECK(!g::SavePreview::decode(no_picture.encode()).thumbnail);++cases;
    auto classified=preview;classified.story_known=true;classified.story_code=122122;const auto classified_copy=g::SavePreview::decode(classified.encode());CHECK(classified_copy.story_known && classified_copy.story_code==122122);++cases;
    classified.story_known=false;rejects([&]{classified.encode();});++guards;classified.story_known=true;classified.story_code=10000000;rejects([&]{classified.encode();});++guards;
    {c::Writer old;for(auto v:{preview.date.year,preview.date.month,preview.date.day,preview.date.weekday,preview.date.hour,preview.date.minute,preview.date.second})old.i32(v);old.u64(preview.saved_ms);old.string(preview.scene);old.string(preview.description);old.boolean(false);const auto legacy=g::SavePreview::decode(old.seal("WASP"));CHECK(!legacy.story_known && !legacy.story_code && !legacy.thumbnail && legacy.date==preview.date);}++cases;
    CHECK(g::save_description("\x81\x40" "ABC\\nDEF")=="ABC" && g::save_description("ABC\nDEF")=="ABC" && g::save_description(std::string(24,'a'))==std::string(24,'a'));++cases;
    CHECK(g::save_description(std::string(25,'a'))==std::string(24,'a')+"\x81\x63");CHECK(g::save_description(std::string(23,'a')+"\x81\x40")==std::string(23,'a')+"\x81\x63");CHECK(g::save_description("<F20"+std::string(30,'a'))=="<F20"+std::string(20,'a')+">\x81\x63");rejects([&]{g::save_description("\x81");});++guards;++cases;
    rejects([&]{g::SavePreviewRequest bad({});});++guards;
    {std::mutex gate;std::condition_variable wake;bool released{};std::atomic<bool> entered{};g::SavePreviewRequest request([&]()->std::optional<g::SavePreview>{std::unique_lock lock(gate);entered=true;CHECK(wake.wait_for(lock,std::chrono::seconds(5),[&]{return released;}));return preview;});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!entered){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(!request.get());{std::lock_guard lock(gate);released=true;}wake.notify_all();while(!request.get()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(request.get()->value->description==preview.description && request.get()->diagnostic.empty());}++cases;
    {g::SavePreviewRequest broken([]()->std::optional<g::SavePreview>{throw std::runtime_error("owned broken preview");});const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!broken.get()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(!broken.get()->value && broken.get()->diagnostic=="owned broken preview");}++cases;
    auto storage=std::make_shared<Memory>();g::SaveSlots slots(storage);CHECK(!slots.preview(3) && !storage->full_reads);slots.write(3,document);CHECK(storage->writes==1 && storage->files[3]==bytes);++cases;
    const auto slot_preview=slots.preview(3);CHECK(slot_preview && slot_preview->description==preview.description && !storage->full_reads && storage->prefix_reads==2 && storage->largest_request==g::SaveDocument::prefix_bytes);CHECK(*slots.read(3)==document.snapshot && storage->full_reads==1);++cases;
    storage->fail=true;auto replacement=document;replacement.preview.description="replacement";rejects([&]{slots.write(3,replacement);});++guards;storage->fail=false;CHECK(storage->writes==1 && slots.preview(3)->description==preview.description && *slots.read(3)==document.snapshot);++cases;
    slots.write(4,document.snapshot);CHECK(slots.preview(4)->legacy && *slots.read(4)==document.snapshot);++cases;
    storage->files[6]=Bytes(16,1);rejects([&]{slots.preview(6);});++guards;rejects([&]{slots.preview(1000);});++guards;
    struct Fallback:g::SaveStorage {Bytes bytes;std::optional<Bytes> read(unsigned)override{return bytes;}void replace(unsigned,std::span<const std::uint8_t>)override{}std::vector<g::SaveSlotInfo> list()override{return {};}};Fallback fallback;fallback.bytes=bytes;CHECK(fallback.prefix(0,8)->size()==8);++cases;
    const auto root=fs::absolute(fs::path(argv[1])).parent_path()/("native-document-owned-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(fs::create_directory(root));const Bytes sentinel{1,3,5,7};file(root/"Sys.sav",sentinel);
    auto disk_storage=wa2::platform::save_storage(root);g::SaveSlots disk(disk_storage);CHECK(!disk.preview(0));disk.write(0,document);CHECK(disk.preview(0)->thumbnail->rgba==picture->rgba && *disk.read(0)==document.snapshot);++cases;
    const auto path=root/"native-saves/slot-000.wa2sav";CHECK(wa2::platform::private_file::read_prefix(path,16,c::maximum_bytes,7)->size()==7);CHECK(wa2::platform::private_file::read_prefix(path,16,c::maximum_bytes,g::SaveDocument::prefix_bytes)->size()==g::SaveDocument::prefix_bytes);++cases;
    rejects([&]{wa2::platform::private_file::read_prefix(path,16,c::maximum_bytes,0);});++guards;rejects([&]{disk_storage->prefix(0,g::SaveDocument::prefix_bytes+1);});++guards;
    fs::create_hard_link(path,root/"native-saves/slot-002.wa2sav");rejects([&]{disk.preview(2);});++guards;rejects([&]{disk.preview(0);});++guards;
    CHECK(*wa2::platform::private_file::read(root/"Sys.sav",0,16)==sentinel);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-save-document.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"bounded_preview_reads\":true,\"independent_preview_checksum\":true,\"legacy_snapshot_loads\":true,\"failed_write_preserves_slot\":true,\"crc_compatible\":true,\"synthetic_files_only\":true,\"original_saves_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native save documents, bounded preview file reads and legacy snapshot compatibility.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
