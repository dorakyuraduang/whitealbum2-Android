// Test the actual PAK reader and its second LZSS loop without game windows.
#include "wa2_win32.hpp"
#include "fast_paths.hpp"
#include "native_resources.hpp"
#include <winrecomp/compiled.hpp>
#include <winrecomp/process.hpp>
#include <winrecomp/hash.hpp>
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <cstring>
using namespace wr;
// Cached empty pools are bounded retained capacity, not live resource buffers.
static auto active_regions(Process& p){
    unsigned large{};
    if(auto found=p.state().reports.find("large_heap_cache");found!=p.state().reports.end()){
        const auto value=found->second();const auto at=value.find("\"idle_blocks\":");
        if(at!=std::string::npos)large=unsigned(std::stoul(value.substr(at+14)));
    }
    return p.memory.regions().size()-p.state().idle_heap_pools-large;
}
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
bool code_page_1075(Cpu&,Memory&,std::uint64_t&);
bool code_page_1076(Cpu&,Memory&,std::uint64_t&);
bool code_page_1079(Cpu&,Memory&,std::uint64_t&);
bool code_page_1111(Cpu&,Memory&,std::uint64_t&);
bool code_page_1112(Cpu&,Memory&,std::uint64_t&);
bool code_page_1141(Cpu&,Memory&,std::uint64_t&);
bool code_page_1150(Cpu&,Memory&,std::uint64_t&);
bool step(Cpu& c,Memory& m,std::uint64_t& b){switch(c.eip>>12){
case 0x433:return code_page_1075(c,m,b);case 0x434:return code_page_1076(c,m,b);
case 0x437:return code_page_1079(c,m,b);
case 0x457:return code_page_1111(c,m,b);case 0x458:return code_page_1112(c,m,b);
case 0x475:return code_page_1141(c,m,b);case 0x47e:return code_page_1150(c,m,b);
default:return false;}}
struct LoopResult {Cpu cpu;std::uint64_t budget,count;std::vector<std::uint8_t> bytes;std::string fault;};
struct LoopCase {std::vector<std::uint8_t> source;U32 maximum{},position{},flags{},defined{},ecx{};unsigned slice{4096};std::uint64_t budget{100000};bool readonly{},corrupt{},stack_readonly{};};
LoopResult loop(const Image& image,const LoopCase& c,bool fast){
    Memory m;CHECK(m.enable_linear_memory());m.map(0x457000,4096,5);m.initialize(0x457000,image.code(0x457000).first(4096));
    m.map(0x07000000,16384,3);m.map(0x20000000,8192,3);m.map(0x21000000,4096,3);m.copy_in(0x21000000,c.source);
    Cpu s;s.eip=0x457090;s.r={0x1234,c.ecx,c.position,0x21000000,0x07000100,0x12345678,0x20000000,0};s.flags=c.flags;s.defined_flags=c.defined;
    const auto sp=s.r[ESP];m.store(sp+0x14,c.maximum,32);m.store(sp+0x1040,U32(c.source.size()),32);
    for(unsigned n=0;n<4096;++n)m.store(sp+0x24+n,(n*37+(n>>3))&255,8);
    if(c.readonly)m.protect(0x20000000,8192,Memory::Read);
    if(c.stack_readonly)m.protect(0x07000000,16384,Memory::Read);
    if(c.corrupt)m.initialize(0x457142,std::array<std::uint8_t,1>{0xcc});
    auto budget=c.budget;std::uint64_t count=0;LoopResult result;
    const auto running=[&]{return s.eip>=0x457086 && s.eip<0x457161;};
    try{while(running()){auto scope=m.execution_scope();unsigned used=0;while(used<c.slice && running()){
        if(fast)if(auto n=wa2_static_batch(s,m,budget,c.slice-used,0xffffffc0)){used+=n;count+=n;continue;}
        tick(budget,s.eip);CHECK(code_page_1111(s,m,budget));++used;++count;
    }}}catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.cpu=s;result.budget=budget;result.count=count;result.bytes.resize(16384+8192);
    m.copy_out(0x07000000,std::span(result.bytes).first(16384));m.copy_out(0x20000000,std::span(result.bytes).subspan(16384));return result;
}
void equal(const LoopResult& a,const LoopResult& b){CHECK(a.cpu.r==b.cpu.r);CHECK(a.cpu.eip==b.cpu.eip);CHECK(a.cpu.flags==b.cpu.flags);CHECK(a.cpu.defined_flags==b.cpu.defined_flags);CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes);CHECK(a.budget==b.budget);CHECK(a.count==b.count);CHECK(a.bytes==b.bytes);CHECK(a.fault==b.fault);}
U32 api(Process& p,const char* name,std::initializer_list<U32> args){return p.callback(p.resolve("kernel32.dll",name),std::span(args.begin(),args.size()));}
struct Sample {std::string archive,name,sha;U32 index,bytes,guest_name{};};
U32 read32(std::istream& f){U32 value{};f.read(reinterpret_cast<char*>(&value),4);CHECK(f);return value;}
int main(int argc,char** argv){try{
    CHECK(argc==5);const auto image=Image::load(argv[1]);std::mt19937 random(0x457090);unsigned cases=0;
    for(unsigned n=0;n<1000;++n){LoopCase c;c.position=random()%4096;c.flags=random();c.defined=random();c.ecx=n%5?0:random();
        for(unsigned g=0;g<1+random()%8;++g){const auto flags=std::uint8_t(random());c.source.push_back(flags);for(unsigned k=0;k<8;++k){c.source.push_back(std::uint8_t(random()));if(flags&(1u<<k))++c.maximum;else{auto high=std::uint8_t(random());c.source.push_back(high);c.maximum+=(high&15)+3;}}}
        c.slice=1+random()%4096;c.budget=n%3?100000:random()%1000;c.readonly=n%31==0;c.corrupt=n%29==0;c.stack_readonly=n%41==0;
        if(n%7==0)c.maximum/=2;if(n%11==0)c.source.pop_back();equal(loop(image,c,false),loop(image,c,true));++cases;
    }
    ProcessOptions options;options.data_root=std::filesystem::path(argv[1]).parent_path()/"res";options.instruction_budget=0x7fffffffffffffffull;
    Process p(step,options);install_wa2_win32(p);p.load(image);
    const auto manager=p.allocate_bytes(4096),archives=p.allocate_bytes(3*0x148),out=p.allocate_bytes(4);
    p.memory.store(manager+0xc,archives,32);p.memory.store(manager+0x10,archives+3*0x148,32);
    std::vector<U32> handles;std::vector<Sample> samples;
    std::ifstream fixture(argv[3],std::ios::binary);std::array<char,8> magic{};fixture.read(magic.data(),8);CHECK((magic==std::array<char,8>{'W','2','R','S','v','1',0,0}));const auto sample_count=read32(fixture);
    for(unsigned n=0;n<sample_count;++n){auto name_size=read32(fixture),compressed=read32(fixture),expanded=read32(fixture);std::string label(name_size,'\0'),sha(64,'\0');fixture.read(label.data(),label.size());fixture.read(sha.data(),64);fixture.seekg(compressed,std::ios::cur);CHECK(fixture);auto slash=label.find('/');CHECK(slash!=std::string::npos);samples.push_back({label.substr(0,slash),label.substr(slash+1),sha,0,expanded});}
    const std::array<std::string,3> names{"bak.pak","char.pak","script.pak"};
    for(unsigned n=0;n<names.size();++n){std::ifstream f(options.data_root/names[n],std::ios::binary);CHECK(f);f.seekg(12);const auto count=read32(f);CHECK(count<65536);const auto table=p.allocate_bytes(count*40),record=archives+n*0x148;
        const auto name=p.put_string(names[n]);const auto handle=api(p,"CreateFileA",{name,0x80000000u,3,0,3,0x80,0});CHECK(handle!=0xffffffffu);handles.push_back(handle);
        p.memory.store(record,handle,32);p.memory.store(record+4,count,32);p.memory.store(record+0x110,table,32);
        for(unsigned k=0;k<count;++k){std::array<std::uint8_t,44> data{};f.read(reinterpret_cast<char*>(data.data()),data.size());CHECK(f);std::array<std::uint8_t,40> entry{};std::copy_n(data.begin()+4,24,entry.begin());entry[31]=data[0]?1:0;
            U32 offset,length;std::memcpy(&offset,data.data()+36,4);std::memcpy(&length,data.data()+40,4);if(entry[31]){offset+=4;length-=4;}std::memcpy(entry.data()+32,&length,4);std::memcpy(entry.data()+36,&offset,4);p.memory.copy_in(table+k*40,entry);
            const std::string filename(reinterpret_cast<const char*>(entry.data()));for(auto& sample:samples)if(sample.archive==names[n] && sample.name==filename)sample.index=k;
        }
    }
    for(auto& sample:samples)sample.guest_name=p.put_string(sample.name);
    const auto baseline_regions=active_regions(p);double load_seconds=0;std::uint64_t decoded=0;unsigned loads=0;
    for(unsigned cycle=0;cycle<40;++cycle)for(const auto& sample:samples){
        const auto archive=unsigned(std::find(names.begin(),names.end(),sample.archive)-names.begin());CHECK(archive<names.size());
        p.cpu.r[ECX]=manager;const auto started=std::chrono::steady_clock::now();const std::array<U32,3> search{archive,sample.guest_name,0};CHECK(p.callback(0x4588a0,search,true)==sample.index);
        const std::array<U32,3> args{archive,sample.index,out};const auto size=p.callback(0x458050,args,true);
        load_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();CHECK(size==sample.bytes);const auto pointer=p.memory.load(out,32);CHECK(pointer && p.memory.load(pointer+size,8)==0);
        std::vector<std::uint8_t> data(size);p.memory.copy_out(pointer,data);CHECK(sha256(data)==sample.sha);const auto heap=api(p,"GetProcessHeap",{});CHECK(api(p,"HeapFree",{heap,0,pointer})==1);p.memory.store(out,0,32);
        CHECK(active_regions(p)==baseline_regions);decoded+=size;++loads;
    }
    // Exercise callers that invoke the complete decoder directly, including
    // its ABI and allocator. None of the decoder's old instructions may run.
    fixture.clear();fixture.seekg(12);unsigned direct_decodes=0;
    for(unsigned n=0;n<sample_count;++n){
        const auto label_size=read32(fixture),compressed=read32(fixture),expanded=read32(fixture);
        fixture.seekg(label_size,std::ios::cur);std::string expected(64,'\0');fixture.read(expected.data(),64);
        std::vector<std::uint8_t> source(compressed);fixture.read(reinterpret_cast<char*>(source.data()),source.size());CHECK(fixture);
        const auto input=p.allocate_bytes(compressed+4);p.memory.store(input,expanded,32);p.memory.copy_in(input+4,source);
        const auto before=p.budget;const std::array<U32,3> args{compressed,input,out};CHECK(p.callback(0x457010,args)==expanded);
        CHECK(before-p.budget<10);const auto output=p.memory.load(out,32);CHECK(output && p.memory.load(output+expanded,8)==0);
        std::vector<std::uint8_t> decoded(expanded);p.memory.copy_out(output,decoded);CHECK(sha256(decoded)==expected);
        const auto heap=api(p,"GetProcessHeap",{});CHECK(api(p,"HeapFree",{heap,0,output}));p.memory.release(input);p.memory.store(out,0,32);
        CHECK(active_regions(p)==baseline_regions);++direct_decodes;
    }
    std::ifstream kcap_fixture(argv[4],std::ios::binary);kcap_fixture.seekg(8);CHECK(read32(kcap_fixture)==sample_count);unsigned image_decodes=0;
    struct ImageSample {std::string name,sha;U32 expanded{};};std::vector<ImageSample> image_samples;
    for(unsigned n=0;n<sample_count;++n){
        const auto label_size=read32(kcap_fixture),compressed=read32(kcap_fixture),expanded=read32(kcap_fixture);
        std::string label(label_size,'\0');kcap_fixture.read(label.data(),label.size());std::string expected(64,'\0');kcap_fixture.read(expected.data(),64);
        image_samples.push_back({label,expected,expanded});
        std::vector<std::uint8_t> source(compressed);kcap_fixture.read(reinterpret_cast<char*>(source.data()),source.size());CHECK(kcap_fixture);
        const auto input=p.allocate_bytes(compressed),output=p.allocate_bytes(expanded+1);p.memory.copy_in(input,source);p.memory.store(output+expanded,0x5a,8);
        const auto before=p.budget;const std::array<U32,4> args{output,input,compressed,expanded};CHECK(p.callback(0x437000,args,false)==expanded);CHECK(before-p.budget<10);
        std::vector<std::uint8_t> decoded(expanded);p.memory.copy_out(output,decoded);CHECK(sha256(decoded)==expected);CHECK(p.memory.load(output+expanded,8)==0x5a);
        p.memory.release(input);p.memory.release(output);CHECK(active_regions(p)==baseline_regions);++image_decodes;
    }
    // Compare complete image loading with the original outer function. Only
    // this reference process declines the new replacement; no timing claim is
    // made from its slower checked-instruction fallback.
    Process reference(step,options);reference.memory.enable_linear_memory();install_wa2_kernel(reference);install_wa2_fast_paths(reference);
    install_wa2_native_resources(reference,false);reference.load(image);
    const auto reference_out=reference.allocate_bytes(4);std::array<std::uint8_t,4096> initial{},actual_dictionary{},reference_dictionary{};
    unsigned image_loads=0;double image_load_seconds=0;
    for(const auto& sample:image_samples){
        const auto slash=sample.name.find('/');CHECK(slash!=std::string::npos);
        const auto archive=p.put_string(sample.name.substr(0,slash)),name=p.put_string(sample.name.substr(slash+1));
        const auto ref_archive=reference.put_string(sample.name.substr(0,slash)),ref_name=reference.put_string(sample.name.substr(slash+1));
        const auto expected_index=reference.callback(0x437220,std::array<U32,2>{ref_archive,ref_name},false);CHECK(expected_index);
        const auto before_check=p.budget;CHECK(p.callback(0x437220,std::array<U32,2>{archive,name},false)==expected_index);CHECK(before_check-p.budget<10);
        reference.memory.copy_in(0xa7cdd0,initial);
        CHECK(reference.callback(0x437310,std::array<U32,3>{ref_archive,ref_name,reference_out},false)==sample.expanded);
        const auto ref_pointer=reference.memory.load(reference_out,32);CHECK(ref_pointer);
        std::vector<std::uint8_t> ref_bytes(sample.expanded);reference.memory.copy_out(ref_pointer,ref_bytes);CHECK(sha256(ref_bytes)==sample.sha);
        reference.memory.copy_out(0xa7cdd0,reference_dictionary);
        const auto ref_heap=api(reference,"GetProcessHeap",{});CHECK(api(reference,"HeapFree",{ref_heap,0,ref_pointer}));
        const auto regions=active_regions(p);
        for(unsigned cycle=0;cycle<8;++cycle){
            p.memory.copy_in(0xa7cdd0,initial);const auto before=p.budget;const auto start=std::chrono::steady_clock::now();
            CHECK(p.callback(0x437310,std::array<U32,3>{archive,name,out},false)==sample.expanded);
            image_load_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();CHECK(before-p.budget<10);
            const auto pointer=p.memory.load(out,32);CHECK(pointer);std::vector<std::uint8_t> data(sample.expanded);p.memory.copy_out(pointer,data);CHECK(data==ref_bytes);
            p.memory.copy_out(0xa7cdd0,actual_dictionary);CHECK(actual_dictionary==reference_dictionary);
            const auto heap=api(p,"GetProcessHeap",{});CHECK(api(p,"HeapSize",{heap,0,pointer})==sample.expanded);CHECK(api(p,"HeapFree",{heap,0,pointer}));
            CHECK(active_regions(p)==regions);++image_loads;
        }
    }
    // Synthetic archive stays in build/, never in the user's resources. Its
    // unsorted duplicate names, raw/empty data and later file replacement
    // exercise directory caching and preserve the original first match.
    const auto fixture_root=std::filesystem::absolute(std::filesystem::path(argv[3])).parent_path();
    const auto image_file=fixture_root/"native-image-loader-fixture.pak";
    auto make_image_archive=[&](std::string first){
        std::vector<std::uint8_t> bytes(16+4*44);std::memcpy(bytes.data(),"KCAP",4);
        auto put32=[&](std::size_t at,U32 value){for(unsigned k=0;k<4;++k)bytes[at+k]=std::uint8_t(value>>(k*8));};put32(12,4);
        auto member=[&](unsigned index,const char* label,unsigned flag,std::span<const std::uint8_t> data){
            const auto record=16+index*44;put32(record,flag);std::memcpy(bytes.data()+record+4,label,std::strlen(label));
            put32(record+36,U32(bytes.size()));put32(record+40,U32(data.size()));bytes.insert(bytes.end(),data.begin(),data.end());};
        member(0,"dup.dat",0,std::span(reinterpret_cast<const std::uint8_t*>(first.data()),first.size()));
        member(1,"DUP.dat",0,std::array<std::uint8_t,3>{'t','w','o'});member(2,"zero.dat",0,{});
        member(3,"packed.dat",1,std::array<std::uint8_t,12>{12,0,0,0,3,0,0,0,7,'a','b','c'});
        std::ofstream output(image_file,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(output);
    };
    make_image_archive("one");p.options.data_root=fixture_root;const auto synthetic_archive=p.put_string(image_file.filename().string());unsigned image_edges=0;
    auto edge=[&](const char* label,const std::string& expected){const auto name=p.put_string(label);const auto regions=active_regions(p);
        CHECK(p.callback(0x437220,std::array<U32,2>{synthetic_archive,name},false)==(std::string(label)=="packed.dat"?4:std::string(label)=="zero.dat"?3:1));
        CHECK(p.callback(0x437310,std::array<U32,3>{synthetic_archive,name,out},false)==expected.size());const auto pointer=p.memory.load(out,32);CHECK(pointer);
        std::vector<std::uint8_t> data(expected.size());p.memory.copy_out(pointer,data);CHECK(std::string(data.begin(),data.end())==expected);
        const auto heap=api(p,"GetProcessHeap",{});CHECK(api(p,"HeapSize",{heap,0,pointer})==expected.size());CHECK(api(p,"HeapFree",{heap,0,pointer}));CHECK(active_regions(p)==regions);++image_edges;};
    edge("dUp.dAt","one");edge("packed.dat","abc");edge("zero.dat","");
    const auto missing=p.put_string("missing.dat");CHECK(p.callback(0x437220,std::array<U32,2>{synthetic_archive,missing},false)==0);CHECK(p.callback(0x437310,std::array<U32,3>{synthetic_archive,missing,0},false)==0);++image_edges;
    const auto absent=p.put_string("archive-that-does-not-exist-3c1a6af2.pak");CHECK(!std::filesystem::exists(fixture_root/"archive-that-does-not-exist-3c1a6af2.pak"));
    const auto before_absent=p.budget;CHECK(p.callback(0x437220,std::array<U32,2>{absent,missing},false)==0);CHECK(p.last_error==2);
    p.memory.store(out,0x5a5a5a5a,32);CHECK(p.callback(0x437310,std::array<U32,3>{absent,missing,out},false)==0);
    CHECK(p.last_error==2 && p.memory.load(out,32)==0x5a5a5a5a);CHECK(before_absent-p.budget<10);++image_edges;
    make_image_archive("newer");edge("dup.dat","newer");p.options.data_root=options.data_root;
    const auto native=p.state().reports.at("native_resources")();CHECK(native.find("\"loads\":244")!=std::string::npos);CHECK(native.find("\"errors\":0")!=std::string::npos);CHECK(native.find("\"declined\":0")!=std::string::npos);CHECK(native.find("\"image_missing_archives\":2")!=std::string::npos);CHECK(direct_decodes==5 && image_decodes==5 && image_loads==40 && image_edges==6);
    // Failed single-pass decoding must not publish a partial image, modify the
    // shared dictionary, or leak its private allocation before the original
    // diagnostic path takes over. Stop at that path instead of executing the
    // original unbounded decoder on deliberately damaged data.
    unsigned transactional_failures=0;
    const std::array<std::vector<std::uint8_t>,6> invalid_streams{{
        {1},{0,0},{0,0,15},{7,'a','b','c','x'},
        {255,'a','b','c','d','e','f','g','h',0,0,15},{1,'a'}
    }};
    const std::array<U32,6> invalid_sizes{3,3,3,3,10,0};
    for(unsigned n=0;n<invalid_streams.size();++n){
        const auto path=fixture_root/("native-image-invalid-"+std::to_string(n)+".pak");
        const auto& stream=invalid_streams[n];std::vector<std::uint8_t> file(16+44+8+stream.size());
        auto put=[&](std::size_t at,U32 v){for(unsigned k=0;k<4;++k)file[at+k]=std::uint8_t(v>>(k*8));};
        std::memcpy(file.data(),"KCAP",4);put(12,1);put(16,1);std::memcpy(file.data()+20,"bad.dat",7);
        put(52,60);put(56,U32(8+stream.size()));put(60,U32(8+stream.size()));put(64,invalid_sizes[n]);
        std::copy(stream.begin(),stream.end(),file.begin()+68);
        {std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(file.data()),file.size());CHECK(output);}
        ProcessOptions isolated_options=options;isolated_options.data_root=fixture_root;
        Process isolated(+[](Cpu&,Memory&,std::uint64_t&){return false;},isolated_options);
        CHECK(isolated.memory.enable_linear_memory());install_wa2_kernel(isolated);
        isolated.state().fast_base=0x400000;isolated.state().fast_entries.resize(0xb0000);
        install_wa2_native_resources(isolated);isolated.load(image);
        api(isolated,"GetProcessHeap",{});
        const auto isolated_out=isolated.allocate_bytes(4),path_name=isolated.put_string(path.filename().string()),member_name=isolated.put_string("bad.dat");
        isolated.memory.store(isolated_out,0x5a5a5a5a,32);
        std::array<std::uint8_t,4096> before_dictionary{},after_dictionary{};
        for(unsigned k=0;k<before_dictionary.size();++k)before_dictionary[k]=std::uint8_t(k*37+n);
        isolated.memory.copy_in(0xa7cdd0,before_dictionary);const auto regions=active_regions(isolated);
        bool declined=false;
        try{isolated.callback(0x437310,std::array<U32,3>{path_name,member_name,isolated_out},false);}
        catch(const GuestFault& fault){declined=fault.kind==FaultKind::untranslated && fault.address==0x437310;}
        CHECK(declined);CHECK(isolated.memory.load(isolated_out,32)==0x5a5a5a5a);
        isolated.memory.copy_out(0xa7cdd0,after_dictionary);CHECK(after_dictionary==before_dictionary);
        CHECK(active_regions(isolated)==regions);
        const auto state=isolated.state().reports.at("native_resources")();
        CHECK(state.find("\"declined\":1")!=std::string::npos && state.find("\"errors\":0")!=std::string::npos);
        ++transactional_failures;
    }
    for(auto h:handles)CHECK(api(p,"CloseHandle",{h})==1);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.pak-loading.v1\",\"exact_loop_cases\":"<<cases<<",\"actual_pak_loads\":"<<loads<<",\"direct_native_decoder_cases\":"<<direct_decodes<<",\"native_image_decoder_cases\":"<<image_decodes<<",\"native_full_image_load_cases\":"<<image_loads<<",\"image_loader_edge_cases\":"<<image_edges<<",\"transactional_decode_failure_cases\":"<<transactional_failures<<",\"image_load_seconds\":"<<image_load_seconds<<",\"original_image_loader_matches\":true,\"decoded_bytes\":"<<decoded<<",\"load_seconds\":"<<load_seconds<<",\"regions_restored_after_every_load\":true,\"reference_hashes_match\":true,\"native_resources\":"<<native<<",\"native_cpu_kernels\":"<<p.state().reports.at("native_cpu_kernels")()<<",\"screen_access\":false,\"gameplay_verified\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS PAK loader: "<<cases<<" exact loop cases, "<<loads<<" real read/decode/free cycles, "<<decoded<<" decoded bytes in "<<load_seconds<<" seconds, reference hashes and guest region counts match; no screen access\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
