#include "io_diagnostics.hpp"
#include "save_files.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace wa2::platform::compat {namespace {
struct State {
    std::filesystem::path path;std::string release;IoDiagnosticSink sink;IoDiagnosticClock now;
    std::uint64_t polls{},attempts{},snapshots{},failures{},last_activity{},last_resources{},last_attempt{};
    std::string report()const{return "{\"snapshots\":"+std::to_string(snapshots)+",\"attempts\":"+std::to_string(attempts)+",\"failures\":"+std::to_string(failures)+",\"maximum_snapshots\":64,\"minimum_interval_ms\":500,\"minimum_resource_interval_ms\":2000,\"screen_access\":false}";}
    void emit(std::string_view key,std::string_view json)noexcept{if(sink)try{sink(release+" #"+std::to_string(snapshots)+" "+std::string(key),json);}catch(...){}}
    void poll(wr::Process& process)noexcept{
        if((++polls&255) || attempts>=64)return;
        const int saved_errno=errno;struct Restore{int value;~Restore(){errno=value;}} restore{saved_errno};
        try{
            const auto activity=save_files_activity(process.state().mutable_files.get()).value_or(0);
            const auto resources=process.state().resource_io_activity?process.state().resource_io_activity():0;
            if(activity==last_activity && resources==last_resources)return;
            const auto interval=activity==last_activity?2000000000ull:500000000ull;
            const auto time=now();if(attempts && (time<last_attempt || time-last_attempt<interval))return;
            ++attempts;last_attempt=time;
            std::map<std::string,std::string> records;
            for(const auto* key:{"selected_directory_io","save_read_scope","file_io","profile_io","native_resources","resource_graphics","scheduler","host_event_pump","frame_work","static_cpu_work","native_image_chroma"}){
                const auto found=process.state().reports.find(key);if(found!=process.state().reports.end())records.emplace(key,found->second());
            }
            std::ostringstream text;text<<"{\"host_release\":"<<wr::quote(release)<<",\"report_kind\":\"file-io-progress\",\"complete\":false,\"error\":\"\",\"sequence\":"<<snapshots+1<<",\"process\":{";bool comma=false;
            for(const auto& [key,value]:records){if(comma)text<<',';comma=true;text<<wr::quote(key)<<':'<<value;}text<<"}}\n";
            const auto bytes=text.str();if(bytes.size()>65536)throw std::runtime_error("I/O progress report exceeds bound");
            auto temporary=path;temporary+=".tmp";
            try{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(bytes.data(),std::streamsize(bytes.size()));out.close();if(!out)throw std::runtime_error("Cannot write I/O progress report");std::filesystem::rename(temporary,path);}
            catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
            ++snapshots;last_activity=activity;last_resources=resources;
            for(const auto* key:{"selected_directory_io","save_read_scope","native_resources","scheduler","host_event_pump","frame_work","static_cpu_work","native_image_chroma"})if(auto found=records.find(key);found!=records.end())emit(key,found->second);
        }catch(...){++failures;emit("snapshot_error","{\"write_failed\":true}");}
    }
};
}
void install_io_diagnostics(wr::Process& process,const std::filesystem::path& final_report,std::string release,IoDiagnosticSink sink,IoDiagnosticClock clock){
    if(final_report.empty())return;
    if(final_report.extension()!=".json")throw std::invalid_argument("I/O diagnostics require a JSON report filename");
    auto state=std::make_shared<State>();state->path=final_report.parent_path()/(final_report.stem().string()+"-progress.json");state->release=std::move(release);state->sink=std::move(sink);
    state->now=clock?std::move(clock):IoDiagnosticClock([]{return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());});
    process.state().reports["io_diagnostics"]=[state]{return state->report();};
    process.state().poll.push_back([&process,state]{state->poll(process);});
}
}
