// Actual cached SC save readers and thumbnail loop, using only invented RAM/files.
#include "../recompiled/special/runtime.hpp"
#include "save_profile.hpp"
#include "../platform/compat/save_files.hpp"
#include "../platform/compat/read_ahead_streams.hpp"
#include <fstream>
#include <iostream>
#include <set>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("save check ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace a=wa2::aot;namespace s=wa2::special;namespace c=wa2::platform::compat;using wr::U32;
constexpr std::string_view bank="Documents/Leaf/WHITE ALBUM2 Special Contents";
std::string lower(std::string_view s){std::string out(s);for(auto& c:out){if(c>='A' && c<='Z')c+=32;if(c=='\\')c='/';}return out;}
class Files final:public wr::MutableFiles,public c::ReadAheadFiles {
public:
    std::map<std::string,std::vector<std::uint8_t>> data;
    unsigned queries{},lists{},opens{},batches{},requested{},maximum{},scopes{};
    std::vector<std::weak_ptr<c::ReadAheadStreams>> pools;
    std::function<void()> observe;
    Files(){for(unsigned n=0;n<100;++n){auto bytes=std::vector<std::uint8_t>(0x10058,n+1);std::fill_n(bytes.begin(),8,0);
        data.emplace(lower(std::string(bank)+"/save_"+char('0'+n/10)+char('0'+n%10)+".sav"),std::move(bytes));}}
    wr::ReadOnlyFileInfo metadata(const std::string& name){
        auto item=wr::ReadOnlyFileInfo{name.substr(name.find_last_of('/')+1),data.at(name).size(),0,128};
        item.open_read=[this,name](U32& error){return open(name,true,false,3,error);};return item;
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;auto found=data.find(lower(name));return found==data.end()?std::nullopt:std::optional(metadata(found->first));}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view path)override{++lists;std::vector<wr::ReadOnlyFileInfo> out;const auto prefix=lower(path)+"/";for(auto& [name,bytes]:data)if(name.starts_with(prefix) && name.find('/',prefix.size())==name.npos)out.push_back(metadata(name));return out;}
    std::FILE* open(std::string_view path,bool,bool,U32,U32& error)override{if(observe)observe();auto found=data.find(lower(path));if(found==data.end()){error=2;return nullptr;}
        ++opens;auto* out=std::tmpfile();CHECK(out);CHECK(std::fwrite(found->second.data(),1,found->second.size(),out)==found->second.size());std::rewind(out);error=0;return out;}
    InventoryList begin_inventory_list()override{++scopes;return [this](std::string_view path){return list(path);};}
    std::size_t prepare_reads(std::span<c::ReadAheadFile> files)override{
        for(const auto& pool:pools)CHECK(pool.expired());CHECK(files.size()>1 && files.size()<=limit);
        ++batches;requested+=files.size();maximum=std::max(maximum,unsigned(files.size()));
        auto pool=std::make_shared<c::ReadAheadStreams>(files.size());pools.push_back(pool);
        for(unsigned n=0;n<files.size();++n){CHECK(lower(files[n].path).starts_with(lower(bank)+"/save_"));U32 error{};
            auto* file=files[n].info->open_read(error);CHECK(file);pool->put(n,file);pool->bind(*files[n].info,n,pool);}
        return files.size();
    }
    bool released()const{for(const auto& p:pools)if(!p.expired())return false;return true;}
    U32 directory(std::string_view)override{return 183;}
    U32 remove(std::string_view path,bool)override{return data.erase(lower(path))?0:2;}
    U32 move(std::string_view,std::string_view)override{return 5;}
    U32 attributes(std::string_view,U32)override{return 0;}
    void replace(std::string_view path,std::span<const std::uint8_t> bytes)override{data[lower(path)]={bytes.begin(),bytes.end()};}
};
struct SaveResult {unsigned readers{},guards{},stack_checks{};};
class Movies final:public wr::ReadOnlyFiles {
public:
    unsigned mask=1,opens{};
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view)const override{return {};}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view path)const override{
        std::vector<wr::ReadOnlyFileInfo> out;if(!path.empty())return out;
        for(const auto* group:{"mv00","mv25"})for(unsigned n=0;n<3;++n)if(mask&(1u<<n))out.push_back({std::string(group)+char('0'+n)+".pak",16,0});return out;
    }
    std::FILE* open(std::string_view)override{++opens;throw std::runtime_error("metadata fixture cannot open a movie payload");}
};
unsigned movie_choices(bool linear){
    wr::ProcessOptions options;options.allow_file_write=true;options.prefer_linear_memory=linear;options.data_root=std::filesystem::current_path()/"build";
    s::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;p.load(s::initial_image());
    auto files=std::make_shared<Files>();auto movies=std::make_shared<Movies>();p.state().mutable_files=s::save_files(files,&p);p.state().read_only_files=movies;
    const auto key=p.put_string("mov_lv"),section=p.put_string("DEFAULT"),ini=p.put_string("SYSTEM.ini"),stem=p.put_string("mv00");
    const auto scratch=p.allocate_bytes(4096);unsigned cases{};
    for(unsigned mask=1;mask<8;++mask)for(unsigned previous=0;previous<4;++previous){
        movies->mask=mask;const auto text="[DEFAULT]\r\nmov_lv="+std::to_string(previous)+"\r\nordinary=17\r\n[other]\r\nkeep=unchanged\r\n";
        p.state().mutable_files->replace("SYSTEM.ini",{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
        // Actual source INI helper -> shared before-read hook -> resource
        // metadata selection, not a test call straight to the selector.
        const auto chosen=p.callback(0x44f210,std::array<U32,4>{key,0,section,ini},false);
        const auto suffix=[](unsigned level){return level==2?0u:level==1?1u:2u;};
        CHECK(chosen<=2 && (mask&(1u<<suffix(chosen))));
        if(previous<=2 && (mask&(1u<<suffix(previous))))CHECK(chosen==previous);
        auto& bytes=files->data.at(lower(std::string(bank)+"/system.ini"));const std::string saved(bytes.begin(),bytes.end());
        CHECK(saved.find("keep=unchanged")!=saved.npos && saved.find("ordinary=17")!=saved.npos);
        // Execute only the real static filename branch. An unmapped movie
        // receiver stops it BEFORE any graph/window/audio work can occur.
        auto cpu=p.cpu;m.store(0x4bb04c,chosen,32);m.store(0xb6c160,0,32);
        p.cpu.r[wr::EBP]=scratch+0x800;p.cpu.r[wr::ESP]=scratch+0x700;p.cpu.r[wr::ESI]=stem;p.cpu.r[wr::EBX]=0;
        bool stopped{};try{p.callback(0x413427,{},false);}catch(const wr::GuestFault& fault){CHECK(fault.kind==wr::FaultKind::memory && fault.address==0x14);stopped=true;}
        CHECK(stopped && p.read_string(scratch+0x800-0x104)=="mv00"+std::to_string(suffix(chosen))+".pak");p.cpu=cpu;++cases;
    }
    // The supplied SC set has mv000/mv250 only. Every invalid old setting
    // above therefore reaches mv000, and valid settings are preserved.
    movies->mask=0;const auto text=std::string("[DEFAULT]\r\nmov_lv=1\r\n");p.state().mutable_files->replace("SYSTEM.ini",{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
    CHECK(p.callback(0x44f210,std::array<U32,4>{key,0,section,ini},false)==1);++cases;
    CHECK(p.state().reports.at("movie_resource_settings")().find("no-movie-files-indexed")!=std::string::npos);
    CHECK(movies->opens==0 && !p.gui() && !p.directdraw());return cases;
}
SaveResult save_readers(bool linear){
    wr::ProcessOptions options;options.allow_file_write=true;options.prefer_linear_memory=linear;
    options.data_root=std::filesystem::current_path()/"build";
    s::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;p.load(s::initial_image());
    auto backend=std::make_shared<Files>();p.state().mutable_files=s::save_files(backend,&p);
    const std::string cwd="C:\\WinRecomp";m.copy_in(0xa36c68,{reinterpret_cast<const std::uint8_t*>(cwd.c_str()),cwd.size()+1});
    m.store(s::save_source.screen,7,32);m.store(s::save_source.update,11,32);const auto output=p.allocate_bytes(32);SaveResult result;
    auto index=[&](unsigned slot){CHECK(p.callback(0x4226d0,std::array<U32,3>{slot,output,0},false)==1);CHECK(m.load(output,8)==slot+1);++result.readers;};
    for(unsigned n=0;n<100;++n)index(n);
    CHECK(backend->queries==0 && backend->lists==1 && backend->batches==7 && backend->requested==100 && backend->maximum==16 && backend->opens==100);
    const auto preview=[&](unsigned slot){CHECK(p.callback(0x4227c0,std::array<U32,6>{slot,0,0,0,0,0},false)==1);CHECK(m.load(0xa4b500+0x10057,8)==slot+1);++result.readers;};
    // Check the real compiled EBP-based helper frames before emulating only
    // their caller return slot. None of the actual page/UI code is executed.
    backend->observe=[&]{const auto sp=p.cpu.r[wr::ESP];CHECK(m.load(sp,32)==s::save_source.helpers[0]);
        CHECK(m.load(sp+s::save_source.api_frame,32)==s::save_source.preview);
        CHECK(m.load(sp+s::save_source.api_frame+8,32)==s::save_source.buffer);
        CHECK(m.load(sp+s::save_source.api_frame+12,32)==0x10058);
        CHECK(m.load(sp+s::save_source.preview_slot,32)==32);++result.stack_checks;};
    preview(32);backend->observe={};CHECK(backend->released() && result.stack_checks==1);
    auto rule=a::save_page_preview_read_ahead(p,s::save_source);U32 page=s::save_source.pages[0];
    auto page_scope=[&]{const auto sp=p.cpu.r[wr::ESP],offset=s::save_source.preview_return;const auto original=m.load(sp+offset,32);
        m.store(sp+offset,page,32);const auto admitted=rule();m.store(sp+offset,original,32);return admitted;};
    p.state().mutable_files=c::save_files(backend,false,a::traced_save_read_scope(p,s::save_source),
        a::save_index_read_ahead(p,s::save_source),a::save_list_metadata_read(p,s::save_source),page_scope,{std::string(bank),true});
    U32 update=12;
    for(auto caller:s::save_source.pages){page=caller;m.store(s::save_source.update,++update,32);const auto before=backend->batches,opens=backend->opens;
        for(unsigned n=20;n<30;++n)preview(n);CHECK(backend->batches==before+1 && backend->opens==opens+10 && backend->maximum==16);++result.guards;}
    // Confirmed/full-load/quick/worker reads cannot speculate adjacent files.
    for(auto caller:{0x409a39u,0x409df9u,0x411122u,0x41b794u,0x41d75eu,0x4238a7u}){
        page=caller;const auto before=backend->batches;preview(32);CHECK(before==backend->batches && backend->released());++result.guards;}
    page=s::save_source.pages[0];p.state().current_thread_id=2;const auto before=backend->batches;preview(32);CHECK(before==backend->batches && backend->released());p.state().current_thread_id=1;++result.guards;
    page=s::save_source.pages[0];m.store(s::save_source.update,21,32);preview(20);CHECK(!backend->released());
    p.state().mutable_files->replace("save_20.sav",std::vector<std::uint8_t>(0x10058,21));CHECK(backend->released());++result.guards;
    backend->data[lower(std::string(bank)+"/save_20.sav")].back()=21;preview(20);
    // Metadata/openers may be reused; contents always come from a new stream.
    backend->data[lower(std::string(bank)+"/save_20.sav")].back()=77;
    CHECK(p.callback(0x4227c0,std::array<U32,6>{20,0,0,0,0,0},false)==1 && m.load(0xa4b500+0x10057,8)==77);++result.readers;++result.guards;
    p.state().mutable_files.reset();CHECK(backend->released() && !p.gui() && !p.directdraw());return result;
}
struct ThumbResult {wr::Cpu cpu;std::vector<std::uint8_t> bytes;U32 fault{};std::uint64_t hits{},copied{};};
ThumbResult thumbnail(bool linear,unsigned shape,bool product){
    constexpr U32 base=0x21000000,size=0x25000,frame=base+0x200;
    wr::Memory m;if(linear)CHECK(m.enable_linear_memory());m.map(base,size,3);ThumbResult out;out.bytes.resize(size);
    for(unsigned n=0;n<size;++n)out.bytes[n]=std::uint8_t(n*71+(n>>7));m.copy_in(base,out.bytes);
    auto& cpu=out.cpu;cpu.eip=s::thumbnail_source.begin;cpu.r[wr::EBP]=frame+48;cpu.r[wr::ESP]=frame;
    cpu.r[wr::EDI]=base+0x1000+(shape==2?3:0);cpu.r[wr::EBX]=base+0x13000+(shape==2?7:0);
    if(shape==6)cpu.r[wr::EBX]=cpu.r[wr::EDI]+4;
    if(shape==7)cpu.r[wr::EBX]=frame;
    cpu.r[wr::EAX]=shape==5?0:128;cpu.r[wr::ESI]=0x9000000;cpu.r[wr::ECX]=0x12345;cpu.r[wr::EDX]=0x6789;
    cpu.flags=0x6d7;cpu.defined_flags=0xff;
    for(unsigned n=0;n<cpu.fp.bytes.size();++n)cpu.fp.bytes[n]=std::uint8_t(n*31);
    m.store(frame+12,shape==3?260:512,32);m.store(frame+40,shape==4?3:128,32);m.store(frame+24,shape==1?30:0,32);
    if(shape==8)m.protect(cpu.r[wr::EDI]+4096,4096,wr::Memory::Write);
    if(shape==9)m.protect(cpu.r[wr::EBX]+4096,4096,wr::Memory::Read);
    if(shape==10)m.protect(cpu.r[wr::EBX]+4096,4096,7);
    if(shape==11)m.protect(base,4096,wr::Memory::Read);
    const auto previous=a::save_thumbnail_counts;
    std::optional<a::SaveThumbnailReferenceScope> original;if(!product)original.emplace();
    auto scope=m.execution_scope();auto location=s::locate(cpu.eip);CHECK(location.function);std::uint64_t budget{};
    try{location.function->body(cpu,m,scope,budget,false,location.resume);throw std::runtime_error("missing thumbnail boundary");}
    catch(const wr::GuestFault& f){CHECK(f.kind==wr::FaultKind::memory);out.fault=f.address;}
    out.hits=a::save_thumbnail_counts.copies-previous.copies;out.copied=a::save_thumbnail_counts.bytes-previous.bytes;
    m.protect(base,size,3);m.copy_out(base,out.bytes);return out;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned readers{},guards{},stack_checks{},thumbs{},hits{},movies{};
    for(bool linear:{false,true}){auto r=save_readers(linear);readers+=r.readers;guards+=r.guards;stack_checks+=r.stack_checks;
        movies+=movie_choices(linear);
        for(unsigned shape=0;shape<12;++shape){auto old=thumbnail(linear,shape,false),candidate=thumbnail(linear,shape,true);
            CHECK(old.cpu.r==candidate.cpu.r && old.cpu.eip==candidate.cpu.eip && old.cpu.flags==candidate.cpu.flags && old.cpu.defined_flags==candidate.cpu.defined_flags);
            CHECK(old.cpu.fp.bytes==candidate.cpu.fp.bytes && old.fault==candidate.fault && old.bytes==candidate.bytes && !old.hits);
            if(shape<=2)CHECK(candidate.hits==1);hits+=candidate.hits;++thumbs;}}
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"actual_static_save_reader_calls\":"<<readers<<",\"guards\":"<<guards
        <<",\"actual_preview_stack_checks\":"<<stack_checks<<",\"exact_thumbnail_cases\":"<<thumbs<<",\"thumbnail_hits\":"<<hits
        <<",\"actual_movie_profile_and_filename_cases\":"<<movies
        <<",\"shared_inventory_and_read_ahead\":true,\"payload_reads_fresh\":true,\"game_launched\":false,\"screen_access\":false}\n";CHECK(out.good());
    std::cout<<"PASS Special save readers and thumbnail comparisons\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
