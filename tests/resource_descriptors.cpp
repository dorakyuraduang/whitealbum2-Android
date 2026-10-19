#include "resource_fd.hpp"
#include "file_io.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
unsigned checks{};
void check(bool value){if(!value)throw std::runtime_error("descriptor check failed");++checks;}
template<class F>void rejects(F&& f){try{f();}catch(const wa2::resource::Error&){++checks;return;}
    throw std::runtime_error("invalid descriptor read accepted");}
int open_file(const std::filesystem::path& path,bool write=false){
#ifdef _WIN32
    return _wopen(path.c_str(),(write?_O_RDWR:_O_RDONLY)|_O_BINARY);
#else
    return open(path.c_str(),(write?O_RDWR:O_RDONLY)|O_CLOEXEC);
#endif
}
bool closed(int fd){
#ifdef _WIN32
    struct _stat64 info{};return _fstat64(fd,&info)!=0;
#else
    struct stat info{};return fstat(fd,&info)!=0;
#endif
}
void close_fd(int fd){
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}
struct Tracked final : wa2::resource::Source {
    std::shared_ptr<wa2::resource::Source> source;
    std::uint64_t read_bytes{};
    explicit Tracked(std::shared_ptr<wa2::resource::Source> value):source(std::move(value)){}
    std::uint64_t size()const override{return source->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override{
        source->read_at(at,bytes);read_bytes+=bytes.size();
    }
};
}
int main(int argc,char** argv){try{
    check(argc==4);
    const auto root=std::filesystem::path(argv[1]),scratch=std::filesystem::path(argv[2]);
    const auto file=scratch/"descriptor-fixture.bin";
    std::array<std::uint8_t,4096> expected{};
    for(unsigned i=0;i<expected.size();++i)expected[i]=std::uint8_t((i*53)^(i>>3));
    {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(expected.data()),expected.size());check(bool(out));}
    const int fd=open_file(file);check(fd>=0);
    auto source=wa2::platform::adopt_resource_fd(fd);
    check(source->size()==expected.size());
    std::array<std::uint8_t,37> bytes{};
    for(unsigned at:{0,17,51,4096-37}){
        source->read_at(at,bytes);check(std::memcmp(bytes.data(),expected.data()+at,bytes.size())==0);
    }
    source->read_at(expected.size(),{});check(true);
    rejects([&]{source->read_at(expected.size()+1,{});});
    rejects([&]{source->read_at(expected.size()-1,bytes);});
    rejects([&]{source->read_at(std::numeric_limits<std::uint64_t>::max(),bytes);});
    std::atomic<bool> okay{true};
    auto reader=[&](unsigned offset){try{
        for(unsigned n=0;n<64;++n){std::array<std::uint8_t,29> actual{};const auto at=offset+n*17;
            source->read_at(at,actual);
            if(std::memcmp(actual.data(),expected.data()+at,actual.size())!=0)okay=false;}
        }catch(...){okay=false;}};
    std::thread a(reader,0),b(reader,1000);a.join();b.join();check(okay.load());
    source.reset();check(closed(fd));
    rejects([]{wa2::platform::adopt_resource_fd(-1);});
    int ends[2]{};
#ifdef _WIN32
    check(_pipe(ends,256,_O_BINARY)==0);
#else
    check(pipe(ends)==0);
#endif
    rejects([&]{wa2::platform::adopt_resource_fd(ends[0]);});
    check(closed(ends[0]));close_fd(ends[1]);

    const int shortened=open_file(file,true);check(shortened>=0);
    source=wa2::platform::adopt_resource_fd(shortened);
#ifdef _WIN32
    check(_chsize_s(shortened,4)==0);
#else
    check(ftruncate(shortened,4)==0);
#endif
    rejects([&]{source->read_at(17,bytes);});source.reset();
    check(closed(shortened));

    const auto empty_file=scratch/"descriptor-empty.bin";
    {std::ofstream out(empty_file,std::ios::binary);check(bool(out));}
    source=wa2::platform::adopt_resource_fd(open_file(empty_file));
    check(source->size()==0);source->read_at(0,{});
    rejects([&]{source->read_at(0,bytes);});source.reset();

    unsigned real_archives=0;std::uint64_t actual_read=0,total_sizes=0;
    for(const char* bank:{"ck-gal.pak","BGM.PAK","fnt.pak"}){
        const auto path=root/bank;
        auto tracked=std::make_shared<Tracked>(wa2::platform::adopt_resource_fd(open_file(path)));
        auto archive=wa2::resource::Archive::open(tracked);
        const auto baseline=wa2::resource::Archive::open(wa2::platform::open_resource_file(path));
        check(archive.members().size()==baseline.members().size());
        for(std::size_t i=0;i<archive.members().size();++i){
            const auto& left=archive.members()[i];const auto& right=baseline.members()[i];
            if(left.name!=right.name || left.offset!=right.offset || left.stored_size!=right.stored_size)
                throw std::runtime_error("archive directory mismatch");
        }
        check(true);
        std::array<std::uint8_t,13> actual{},expected_tail{};
        tracked->read_at(tracked->size()-actual.size(),actual);
        wa2::platform::open_resource_file(path)->read_at(tracked->size()-actual.size(),expected_tail);
        check(actual==expected_tail);
        if(std::string_view(bank)=="ck-gal.pak"){
            const auto member=archive.find("Global.vrb");check(member!=wa2::resource::Archive::missing);
            check(archive.load(member)==baseline.load(baseline.find("Global.vrb")));
        }
        check(tracked->read_bytes<256*1024);actual_read+=tracked->read_bytes;total_sizes+=tracked->size();
        ++real_archives;
    }
    std::ofstream report(argv[3]);
    report<<"{\"schema\":\"wa2.resource-descriptors.v1\",\"checks\":"<<checks<<
        ",\"real_archives\":"<<real_archives<<",\"source_bytes_read\":"<<actual_read<<
        ",\"archive_total_bytes\":"<<total_sizes<<
        ",\"whole_archives_extracted\":false,\"resources_copied_into_apk\":false,"
        "\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS resource descriptors: "<<checks<<" checks, "<<actual_read<<" bytes from "<<real_archives<<" original PAKs\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
