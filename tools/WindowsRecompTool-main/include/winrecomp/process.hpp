#pragma once
#include "winrecomp/image.hpp"
#include "winrecomp/compiled.hpp"
#include "winrecomp/gui.hpp"
#include "winrecomp/directdraw.hpp"
#include <functional>
#include <memory>
#include <deque>
#include <filesystem>
#include <optional>
#include <string_view>
namespace wr {
using StepFunction=bool(*)(Cpu&,Memory&,std::uint64_t&);
class Process;
struct FunctionExecutor {std::function<bool(Cpu&,Memory&)> invoke;};
struct ProcessBackends {
    std::function<std::unique_ptr<GuiBackend>(Process&)> gui;
    std::function<std::unique_ptr<DirectDrawBackend>(Process&)> directdraw;
};
struct ProcessOptions {
    U32 stack_base{0x07000000},stack_size{0x00100000},teb{0x7ffde000};
    std::uint64_t instruction_budget{10000000};
    std::filesystem::path data_root{"."};
    std::string command_line{"program.exe"},image_name{"program.exe"};
    bool allow_file_write{false};
    bool profile_execution{false};
    // A capability preference, not a different execution core. Hosts with
    // larger allocation pages use the same checked paged-memory representation.
    bool prefer_linear_memory{true};
    std::string user_name{"WinRecomp"};
};
class Process {
public:
    struct Api { std::string dll,name;unsigned arguments{};bool caller_cleans_stack{};std::function<U32(Process&)> function;std::string trace_name;bool leaf{};std::uint64_t calls{},timed_calls{},sampled_ns{},max_sample_ns{}; };
    struct Transfer {
        U32 pc{},return_address{};std::string_view api;
        std::array<U32,64> arguments{};unsigned argument_count{};std::string detail;
    };
    // Reuse 32 inline records, including argument storage. API names refer to
    // stable Api owners; no allocator or name copy runs for ordinary calls.
    class TransferHistory {
        std::array<Transfer,32> records_{};unsigned next_{},count_{};
    public:
        Transfer& append(){auto& value=records_[next_];next_=(next_+1)&31;if(count_<32)++count_;return value;}
        unsigned size()const{return count_;}bool empty()const{return !count_;}
        const Transfer& operator[](unsigned index)const{return records_[(next_+32-count_+index)&31];}
        const Transfer& back()const{return (*this)[count_-1];}
        struct Iterator {
            const TransferHistory* history;unsigned index;
            const Transfer& operator*()const{return (*history)[index];}
            Iterator& operator++(){++index;return *this;}
            bool operator!=(const Iterator& other)const{return index!=other.index;}
        };
        Iterator begin()const{return {this,0};}Iterator end()const{return {this,count_};}
    };
private:
    std::unique_ptr<struct Win32State> win32_;
    std::unique_ptr<GuiBackend> gui_;
    std::unique_ptr<DirectDrawBackend> directdraw_;
    std::unique_ptr<Image> source_image_;
    // Thunks are allocated in contiguous 16-byte slots. Keep each Api stable
    // when a callback registers more interfaces and grows the dispatch vector.
    std::vector<std::unique_ptr<Api>> apis_;
    std::span<const U32> api_arguments_;
    U32 api_argument_stack_{},api_argument_pc_{};
    std::map<std::string,U32> api_names_;
    struct NativeFunction {
        unsigned arguments{};bool caller_cleans_stack{};
        std::function<std::optional<U32>(Process&)> function;
    };
    std::map<U32,NativeFunction> native_functions_;
    bool dispatch_native_function();
    std::map<std::string,U32> modules_;
    U32 next_thunk_{0xf0000000},next_module_{0xe0000000};
    StepFunction step_{};
    FunctionExecutor functions_;
    void initialize(ProcessBackends);
    bool loaded_{},exited_{};
    U32 exit_code_{};
    unsigned profile_countdown_{4093};
    std::map<U32,std::uint64_t> profile_samples_;
    std::map<U32,std::uint64_t> profile_wall_ns_;
    std::map<std::string,std::uint64_t> profile_api_ns_;
    std::string wall_profile() const;
    std::string api_call_profile() const;
    unsigned execute_instructions(U32 return_pc,unsigned count,const char* context);
    bool dispatch_api_boundary();
    void poll_services();

public:
    Cpu cpu;
    Memory memory;
    ProcessOptions options;
    U32 image_base{},image_size{},entry{},last_error{};
    std::string input_sha256;
    std::uint64_t budget{},executed_instructions{},api_calls{},native_function_entries{};
    std::uint64_t direct_leaf_api_calls{};
    std::uint64_t api_stack_snapshots{},api_stack_slow_snapshots{};
    struct HostWork {std::uint64_t sleep_calls{},sleep_zero_calls{},sleep_requested_ms{},sleep_wall_ns{},wait_calls{},wait_wall_ns{},function_wall_ns{},api_wall_ns{},poll_wall_ns{};} host_work;
    TransferHistory recent_transfers;
    explicit Process(StepFunction step,ProcessOptions opts={});
    explicit Process(FunctionExecutor,ProcessOptions opts={},ProcessBackends backends={});
    ~Process();
    Process(const Process&)=delete;Process& operator=(const Process&)=delete;
    void load(const Image& image);

    void register_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool caller_cleans_stack=false);
    // Platform adapters may replace a bootstrap implementation before load.
    // The thunk address and source ABI stay stable for cached callers.
    void replace_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool caller_cleans_stack=false);
    // One ABI conversion for a complete source-level engine function. The
    // target's generated batches must also end before every registered entry.
    // A null result declines the replacement before changing observable state.
    void register_native_function(U32 address,unsigned arguments,
        std::function<std::optional<U32>(Process&)> fn,bool caller_cleans_stack=false);
    U32 resolve(const std::string& dll,const std::string& name,bool allow_unimplemented=false);
    U32 module(const std::string& dll,bool create=false);
    std::string module_name(U32 handle) const;
    bool dispatch_api();
    // Certification belongs to the installed implementation. Replacing that
    // implementation clears it. A leaf must never wait, callback, yield, exit,
    // or transfer the CPU. Thread-owned device state is allowed, with no
    // message pump, presentation callback or scheduler operation.
    void certify_leaf_api(const std::string& dll,const std::string& name);
    bool dispatch_leaf_api();
    U32 argument(unsigned index);
    // Values copied before the current host API executes. Structured wrappers
    // may use this span instead of reading the same guest stack a second time.
    // The span is valid only during that API; callback scopes restore it.
    std::span<const U32> argument_snapshot() const {
        return cpu.r[ESP]==api_argument_stack_ && cpu.eip==api_argument_pc_?api_arguments_:std::span<const U32>{};
    }
    U32 callback(U32 address,std::span<const U32> args,bool stdcall=true);
    U32 run();
    void exit(U32 code){exited_=true;exit_code_=code;}
    bool exited() const{return exited_;}
    bool function_execution() const noexcept{
#ifdef WA2_STATIC_FUNCTIONS_ONLY
        return true;
#else
        return bool(functions_.invoke);
#endif
    }
    U32 virtual_alloc(U32 address,U32 size,U32 allocation,U32 protection);
    bool virtual_free(U32 address,U32 size,U32 operation);
    bool virtual_protect(U32 address,U32 size,U32 protection,U32 old_protection);
    U32 allocate_bytes(std::size_t size,unsigned permission=Memory::Read|Memory::Write);
    std::string read_string(U32 address,std::size_t maximum=32768);
    U32 string_length(U32 address,std::size_t maximum=32768);
    std::u16string read_wstring(U32 address,std::size_t maximum=32768);
    U32 put_string(const std::string& string);
    U32 put_wstring(const std::u16string& string);
    void set_error(U32 value);
    Win32State& state();
    const Image& source_image() const;
    GuiBackend* gui() {return gui_.get();}
    const DirectDrawBackend* directdraw() const {return directdraw_.get();}
    const GuiBackend* gui() const {return gui_.get();}
    std::string report() const;
};
void install_win32(Process& process);
int run_program(int argc,char** argv,StepFunction step,const char* expected_sha256,void(*install_target)(Process&)=nullptr,std::span<const std::uint8_t> embedded_image={});
}
