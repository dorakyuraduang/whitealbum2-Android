#pragma once
#include "winrecomp/runtime.hpp"
#include "winrecomp/readonly_files.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
namespace wr {
class Process;
// Resolve a guest filename under the process data root, including its virtual cwd.
std::filesystem::path guest_file_path(Process&,const std::string&);
struct Win32State {
    struct HeapBlock {U32 size{},capacity{},pool{};};
    struct HeapPool {U32 live{},largest{};std::map<U32,U32> free;};
    struct Heap {U32 maximum{};std::map<U32,HeapBlock> blocks;std::map<U32,HeapPool> pools;std::uint64_t requested_bytes{};U32 idle_pool{};};
    struct File { std::FILE* stream{};bool owned{},read{},write{};U32 type{1};std::filesystem::path path;U32 share{7}; };
    struct Find {
        std::uintptr_t native{};std::function<void()> close;
        std::vector<std::filesystem::path> entries;std::size_t next{};
        bool mounted{};std::vector<ReadOnlyFileInfo> mounted_entries;
        ~Find(){if(close)close();}
    };
    std::map<U32,std::shared_ptr<Find>> finds;
    struct NativeEvent {std::uintptr_t native{};std::function<void()> close,signal;~NativeEvent(){if(close)close();}};
    std::map<U32,std::shared_ptr<NativeEvent>> events;
    std::map<U32,Heap> heaps;
    // One idle 64 KiB pool per heap, at most sixteen for this process.
    // The policy switch is also used by bounded in-memory A/B checks.
    bool retain_idle_heap_pools{true};
    unsigned idle_heap_pools{},peak_idle_heap_pools{};
    std::uint64_t heap_pool_reuses{},heap_pool_retained{},heap_pool_evictions{};
    std::map<U32,File> files;
    std::shared_ptr<ReadOnlyFiles> read_only_files;
    std::map<U32,U32> tls;
    std::set<U32> tls_used;
    std::map<std::string,std::string> environment;
    std::string current_directory;
    struct SelectedPath {std::filesystem::path path;bool directory{};};
    // Populated only by an accepted native file/folder picker selection.
    std::map<std::string,SelectedPath> selected_paths;
    std::function<std::uintptr_t(U32)> native_pidl;
    std::vector<std::function<void()>> shutdown;
    std::map<std::string,std::function<std::string()>> reports;
    std::vector<std::function<void()>> poll;
    unsigned main_poll_interval{4096};
    bool sample_fallback{};
    unsigned fallback_countdown{1021};
    std::map<std::pair<U32,U32>,std::uint64_t> fallback_samples;
    std::function<void(U32,unsigned)> execute_slice;
    // Optional target-specific, statically compiled instruction batches. They
    // preserve exact guest instruction counts, flags, faults and poll limits.
    U32 fast_base{};
    std::vector<std::uint8_t> fast_entries;
    unsigned (*fast_step)(Cpu&,Memory&,std::uint64_t&,unsigned,U32){};
    std::uint64_t fast_batches{},fast_instructions{};
    std::map<U32,std::array<std::uint64_t,5>> fast_profile;
    std::uint64_t block_batches{},block_instructions{};
    unsigned (*block_step)(Cpu&,Memory&,std::uint64_t&,unsigned,U32){};
    bool yield_requested{};
    // A cooperative wait leaves the API stack/PC intact until it becomes
    // runnable. This is ordinary control flow, not a C++ exception.
    bool api_retry{};
    U32 current_thread_id{1};
    std::function<U32(std::span<const U32>,bool,U32)> wait_objects;
    std::function<void(U32)> sleep_thread,wait_critical;
    std::vector<std::function<void(U32)>> thread_exit;
    std::filesystem::path snapshot_path;
    std::uint64_t stop_after_presents{};
    bool diagnostic_stop{};
    std::map<U32,std::size_t> environment_blocks;
    std::map<U32,U32> global_blocks;
    std::set<U32> virtual_reservations;
    std::set<U32> critical_sections;
    std::map<U32,unsigned> critical_depth;
    std::map<U32,U32> critical_owner;
    struct Mutex {
        std::string name;unsigned depth{};std::uintptr_t native{};
        std::function<void()> close;
        ~Mutex(){if(close)close();}
    };
    std::map<U32,std::shared_ptr<Mutex>> mutexes;
    std::map<std::string,std::weak_ptr<Mutex>> mutex_names;
    U32 next_heap{0xd0000000},next_handle{0xc0000000},process_heap{},command_line{},wide_command_line{};
    U32 stdin_handle{},stdout_handle{},stderr_handle{};
    unsigned callback_depth{};
    U32 unhandled_filter{};
    std::optional<Cpu> api_transfer;
    std::chrono::steady_clock::time_point clock_start=std::chrono::steady_clock::now();
    ~Win32State(){for(auto& [id,file]:files){(void)id;if(file.owned && file.stream)std::fclose(file.stream);}}
};
}
