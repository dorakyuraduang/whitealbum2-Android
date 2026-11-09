#include "process.hpp"
#include "../platform/compat/io_diagnostics.hpp"
#include "../platform/compat/save_files.hpp"
#include <winrecomp/win32_state.hpp>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace c=wa2::platform::compat;
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);CHECK(file.good());return {std::istreambuf_iterator<char>(file),{}};}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("io-progress-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    const auto selected=root/"selected",private_root=root/"private";CHECK(std::filesystem::create_directory(selected) && std::filesystem::create_directory(private_root));
    wr::ProcessOptions options;options.data_root=private_root;wa2::aot::Runtime runtime(options);auto& p=runtime.process();runtime.load();
    p.state().mutable_files=c::save_files(c::local_files(selected),false);p.state().reports["selected_directory_io"]=[&]{return c::save_files_report(p.state().mutable_files);};
    std::uint64_t now=1;unsigned events{};c::install_io_diagnostics(p,private_root/"wa2-runtime.json","fixture-release",[&](std::string_view label,auto){if(label.ends_with(" selected_directory_io"))++events;throw std::runtime_error("Optional sink failure");},[&]{return now;});
    const auto poll=[&]{for(unsigned n=0;n<256;++n)p.state().poll.back()();};const auto touch=[&]{p.state().mutable_files->info("save_00.sav");};const auto progress=private_root/"wa2-runtime-progress.json";
    poll();CHECK(!std::filesystem::exists(progress));touch();p.last_error=123;errno=ERANGE;poll();CHECK(errno==ERANGE && p.last_error==123);
    auto first=read(progress);CHECK(first.find("\"report_kind\":\"file-io-progress\"")!=std::string::npos && first.find("\"complete\":false")!=std::string::npos && first.find("\"sequence\":1")!=std::string::npos && events==1);
    touch();now=400000001;poll();CHECK(read(progress)==first);now=500000001;poll();CHECK(read(progress).find("\"sequence\":2")!=std::string::npos);
    const auto second=read(progress);now+=1000000000;poll();CHECK(read(progress)==second);
    // Resource work alone must refresh the snapshot while skipping, without
    // waiting for a save/config read. Sampling never reads image pixels.
    std::uint64_t resource_activity=1;p.state().resource_io_activity=[&]{return resource_activity;};
    now+=2000000000;
    poll();const auto resource_snapshot=read(progress);CHECK(resource_snapshot.find("\"sequence\":3")!=std::string::npos);
    now+=1000000000;poll();CHECK(read(progress)==resource_snapshot);
    ++resource_activity;poll();CHECK(read(progress)==resource_snapshot);now+=1000000000;poll();CHECK(read(progress).find("\"sequence\":4")!=std::string::npos);
    for(unsigned n=0;n<80;++n){touch();now+=500000000;poll();}
    const auto bounded=p.state().reports.at("io_diagnostics")();
    const auto number=[&](const char* key){const auto at=bounded.find('"'+std::string(key)+"\":");CHECK(at!=std::string::npos);return std::stoul(bounded.substr(at+std::strlen(key)+3));};
    // The contract bounds attempts too. An isolated transient filesystem
    // failure may consume an attempt, without violating the snapshot cap.
    CHECK(number("attempts")==64 && number("snapshots")==events && events>=4 && number("snapshots")+number("failures")==64);
    CHECK(std::filesystem::is_empty(selected));CHECK(!std::filesystem::exists(private_root/"wa2-runtime.json") && !std::filesystem::exists(progress.string()+".tmp"));
    CHECK(!p.gui() && !p.directdraw());
    // Failed private output is optional and retried; neither selected files
    // nor guest LastError/errno are changed by a diagnostic failure.
    wr::ProcessOptions other_options;other_options.data_root=private_root;wa2::aot::Runtime other(other_options);auto& q=other.process();other.load();q.state().mutable_files=c::save_files(c::local_files(selected),false);
    q.state().reports["selected_directory_io"]=[&]{return c::save_files_report(q.state().mutable_files);};
    const auto late=private_root/"late";std::uint64_t retry_time=1;c::install_io_diagnostics(q,late/"wa2-runtime.json","retry",{},[&]{return retry_time;});q.state().mutable_files->info("save_01.sav");
    q.last_error=17;errno=EDOM;for(unsigned n=0;n<256;++n)q.state().poll.back()();CHECK(errno==EDOM && q.last_error==17);CHECK(q.state().reports.at("io_diagnostics")().find("\"failures\":1")!=std::string::npos);
    CHECK(std::filesystem::create_directory(late));retry_time+=500000000;for(unsigned n=0;n<256;++n)q.state().poll.back()();CHECK(std::filesystem::is_regular_file(late/"wa2-runtime-progress.json"));
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.io-diagnostics.v1\",\"atomic_replacement\":true,\"unchanged_io_not_rewritten\":true,\"resource_activity_sampled\":true,\"rate_and_count_bounded\":true,\"failed_write_retry\":true,\"optional_sink_failure_isolated\":true,\"guest_errors_preserved\":true,\"selected_directory_written\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());
    std::cout<<"PASS bounded private I/O snapshots, atomic replacement, retry, sink isolation and unchanged game files/errors.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
